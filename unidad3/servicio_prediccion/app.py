import os
import ssl
import json
import time
import numpy as np
from datetime import datetime, timedelta, timezone
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS
import paho.mqtt.client as mqtt

# ------------------------------------------------------------------
#  Configuracion (variables de entorno con valores por defecto)
# ------------------------------------------------------------------
INFLUX_URL    = os.getenv("INFLUX_URL", "http://influxdb:8086")
INFLUX_TOKEN  = os.getenv("INFLUX_TOKEN", "smarthome-token-super-secreto")
INFLUX_ORG    = os.getenv("INFLUX_ORG", "smarthome")
INFLUX_BUCKET = os.getenv("INFLUX_BUCKET", "sensores")

MQTT_HOST = os.getenv("MQTT_HOST", "mosquitto")
MQTT_PORT = int(os.getenv("MQTT_PORT", "8883"))
MQTT_USER = os.getenv("MQTT_USER", "prediccion")
MQTT_PASS = os.getenv("MQTT_PASS", "zapato123")
MQTT_CA   = os.getenv("MQTT_CA", "/certs/ca.crt")
BASE      = os.getenv("MQTT_BASE", "smarthome/equipoXX")

HORIZON_MIN = 30           # proyeccion a 30 minutos
INTERVAL_S  = 600          # se ejecuta cada 10 minutos
VENTANA     = "-6h"        # historial que consulta
MEASUREMENT = "ambiente"

# umbrales criticos (coherentes con la logica del LLM)
UMBRAL = {"temperatura": 40.0, "gas": 2500.0}


def consultar_serie(influx, field):
    """Devuelve (timestamps_segundos, valores) de las ultimas 6 h."""
    q = f'''
    from(bucket:"{INFLUX_BUCKET}")
      |> range(start: {VENTANA})
      |> filter(fn:(r) => r._measurement == "{MEASUREMENT}" and r._field == "{field}")
      |> keep(columns:["_time","_value"])
    '''
    ts, vs = [], []
    for tabla in influx.query_api().query(q, org=INFLUX_ORG):
        for rec in tabla.records:
            ts.append(rec.get_time().timestamp())
            vs.append(rec.get_value())
    return ts, vs


def ajustar_y_proyectar(ts, vs):
    """Regresion lineal (polyfit grado 1). Devuelve (valor_previsto, m, b)."""
    if len(vs) < 2:
        return None, None, None
    t0 = ts[-1]                                   # el ultimo dato es x=0
    x = np.array([(t - t0) / 60.0 for t in ts])   # minutos (pasado = negativo)
    y = np.array(vs)
    m, b = np.polyfit(x, y, 1)                     # y = m*x + b
    valor_previsto = m * HORIZON_MIN + b           # proyeccion a +30 min
    return float(valor_previsto), float(m), float(b)


def minutos_para_umbral(m, b, umbral):
    """Minutos futuros en que la recta cruza el umbral, si es dentro de 30 min."""
    if m <= 0:
        return None                                # tendencia plana o a la baja
    x_cruce = (umbral - b) / m                      # resuelve m*x + b = umbral
    if 0 < x_cruce <= HORIZON_MIN:
        return x_cruce
    return None


def ciclo(influx, cli, write_api):
    for field in ("temperatura", "gas"):
        ts, vs = consultar_serie(influx, field)
        valor, m, b = ajustar_y_proyectar(ts, vs)
        if valor is None:
            print(f"[{field}] datos insuficientes ({len(vs)} pts), se omite", flush=True)
            continue

        # 9.3 - publicar la prediccion
        pred = {"valor": round(valor, 2), "horizon_min": HORIZON_MIN}
        cli.publish(f"{BASE}/prediccion/{field}", json.dumps(pred))
        print(f"[{field}] prediccion +{HORIZON_MIN}min = {pred['valor']}  (n={len(vs)}, pendiente={round(m,4)}/min)", flush=True)

        # 9.5 - guardar la proyeccion en InfluxDB con timestamp a +30 min
        #        (para superponerla al valor real en Grafana)
        punto = Point("prediccion").field(field, round(valor, 2)).time(
            datetime.now(timezone.utc) + timedelta(minutes=HORIZON_MIN))
        write_api.write(bucket=INFLUX_BUCKET, org=INFLUX_ORG, record=punto)

        # 9.4 - alerta preventiva si superara el umbral dentro de 30 min
        dt = minutos_para_umbral(m, b, UMBRAL[field])
        if valor > UMBRAL[field] or dt is not None:
            alerta = {
                "tipo": "preventiva",
                "variable": field,
                "valor_previsto": round(valor, 2),
                "umbral": UMBRAL[field],
                "min_para_umbral": round(dt, 1) if dt is not None else None,
            }
            cli.publish(f"{BASE}/alerta", json.dumps(alerta))
            print(f"  !! ALERTA PREVENTIVA: {alerta}", flush=True)


def main():
    influx = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
    write_api = influx.write_api(write_options=SYNCHRONOUS)

    cli = mqtt.Client(client_id="servicio_prediccion")
    cli.username_pw_set(MQTT_USER, MQTT_PASS)
    cli.tls_set(ca_certs=MQTT_CA, tls_version=ssl.PROTOCOL_TLSv1_2)
    cli.tls_insecure_set(True)      # cert autofirmado: no verificamos hostname
    cli.connect(MQTT_HOST, MQTT_PORT, 60)
    cli.loop_start()
    print("servicio de prediccion iniciado", flush=True)

    while True:
        try:
            ciclo(influx, cli, write_api)
        except Exception as e:
            print("error en el ciclo:", e, flush=True)
        time.sleep(INTERVAL_S)


if __name__ == "__main__":
    main()
