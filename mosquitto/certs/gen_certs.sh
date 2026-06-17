#!/bin/sh
# Genera CA + certificado de servidor autofirmado para Mosquitto (MQTT-TLS, puerto 8883).
# Uso (desde la raiz del proyecto):
#   docker run --rm -v "${PWD}\mosquitto\certs:/certs" -e BROKER_IP=192.168.137.1 --entrypoint sh alpine/openssl /certs/gen_certs.sh
# Cambia BROKER_IP por la IP (o hostname) por la que el ESP32/Node-RED se conectan al broker.

set -e

BROKER_IP="${BROKER_IP:-192.168.137.1}"
DAYS=365
cd /certs

echo "==> Generando para BROKER_IP=$BROKER_IP (validez ${DAYS} dias)"

# 1) Autoridad Certificadora (CA): llave + certificado autofirmado
openssl req -new -x509 -days "$DAYS" -extensions v3_ca -nodes \
  -keyout ca.key -out ca.crt \
  -subj "/C=CL/ST=Maule/L=Talca/O=SmartHomeIoT/CN=SmartHome-CA"

# 2) Llave del servidor + solicitud de firma (CSR). CN = direccion del broker.
openssl req -new -nodes \
  -keyout server.key -out server.csr \
  -subj "/C=CL/ST=Maule/L=Talca/O=SmartHomeIoT/CN=$BROKER_IP"

# 3) Extension SAN (necesaria para validacion estricta por IP/hostname)
cat > extfile.cnf <<EOF
subjectAltName = IP:$BROKER_IP
EOF

# 4) La CA firma el certificado del servidor
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
  -CAcreateserial -out server.crt -days "$DAYS" -extfile extfile.cnf

# Limpieza de temporales
rm -f server.csr extfile.cnf ca.srl

echo "==> Listo. Archivos en mosquitto/certs/:"
ls -l ca.crt ca.key server.crt server.key
