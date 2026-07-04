 análisis de vulnerabilidad


## 5.1 — medidas de seguridad implementadas

El diseño de seguridad del broker se apoya en tres pilares: **autenticación** (quién puede conectarse), **cifrado en tránsito** (TLS) y **autorización por tópico** (ACLs). Todo se aplica sobre el broker Mosquitto desplegado en Docker

### 5.1.1 autenticación de clientes (sin acceso anónimo)

Configuración en [`mosquitto/mosquitto.conf`](../mosquitto/mosquitto.conf):

conf
allow_anonymous false
password_file /mosquitto/config/passwordfile


- `allow_anonymous false` obliga a que **todo** cliente presente usuario y contraseña; una conexión sin credenciales es rechazada por el broker
- Las credenciales se almacenan en [`mosquitto/passwordfile`](../mosquitto/passwordfile) con las contraseñas **hasheadas** por `mosquitto_passwd` (PBKDF2-SHA512), nunca en texto plano
- Usuarios definidos, uno por rol/dispositivo

| Usuario    | Rol                                  |
|------------|--------------------------------------|
| `esp32`    | Nodo de sensores y actuadores        |
| `esp32cam` | Cámara (publica capturas)            |
| `nodered`  | Orquestador / dashboard              |
| `admin`    | Administración y depuración          |

**Evidencia de cierre:** una conexión anónima (`mosquitto_sub` sin `-u/-P`) es rechazada con `Connection Refused: not authorised`

### 5.1.2 cifrado en tránsito con TLS (puerto 8883)

Configuración del listener seguro en [`mosquitto/mosquitto.conf`](../mosquitto/mosquitto.conf):

```conf
listener 8883
cafile   /mosquitto/config/certs/ca.crt
certfile /mosquitto/config/certs/server.crt
keyfile  /mosquitto/config/certs/server.key
tls_version tlsv1.2
require_certificate false
```

- **Único listener expuesto: 8883 (TLS).** El listener 1883 (MQTT en claro) está **comentado**; ni siquiera se publica en la red. En [`docker-compose.yml`](../docker-compose.yml) solo se mapea `"8883:8883"`
- **PKI propia.** Se generó una Autoridad Certificadora (CA) autofirmada y un certificado de servidor firmado por ella, mediante el script [`mosquitto/certs/gen_certs.sh`](../mosquitto/certs/gen_certs.sh):
  - CA: `CN=SmartHome-CA` (`ca.crt` / `ca.key`)
  - Servidor: `CN=192.168.137.1`, con extensión **SAN `IP:192.168.137.1`** para validación estricta por IP
  - Validez: 365 días (`notAfter = Jun 17 2027`)
- **TLS de servidor.** Los clientes validan la identidad del broker contra la CA, pero no presentan certificado propio (`require_certificate false`). Es suficiente para el alcance del curso; el camino a TLS mutuo se discute en 5.2
- **TLS 1.2** fijo, por compatibilidad con `WiFiClientSecure` del ESP32
- **Cliente ESP32** ([`codigo_ESP32.txt`](../codigo_ESP32.txt)): usa `WiFiClientSecure` + `PubSubClient`, carga la CA con `espClient.setCACert(ca_cert)` y conecta al puerto 8883 con usuario y contraseña


### 5.1.3 autorización por tópico (ACLs)

Configuración en [`mosquitto/aclfile`](../mosquitto/aclfile), aplicando **mínimo privilegio** por cliente:

```conf
user esp32
topic write smarthome/equipoXX/sensores
topic read  smarthome/equipoXX/control/buzzer
...

user esp32cam
topic write smarthome/equipoXX/camara

user nodered
topic readwrite smarthome/equipoXX/#

user admin
topic readwrite #
```

- **`esp32`** solo puede **publicar** en su tópico de sensores y **suscribirse** a los de control/actuación. No puede leer la cámara de otros ni escribir en tópicos ajenos
- **`esp32cam`** solo puede **publicar** la imagen; no tiene ningún permiso de lectura
- **`nodered`** tiene acceso amplio pero **acotado al namespace del hogar** (`smarthome/equipoXX/#`), porque es el orquestador
- **`admin`** es el único con comodín global `#`; se reserva para administración

**Efecto:** aunque un dispositivo se autentique correctamente, la ACL limita el daño si sus credenciales se ven comprometidas

### 5.1.4 resumen en capas

| Capa                | Mecanismo                              | Archivo |
|---------------------|----------------------------------------|---------|
| Identidad           | Usuario + contraseña hasheada          | `passwordfile` |
| Confidencialidad    | TLS 1.2 en 8883, CA propia             | `mosquitto.conf`, `certs/` |
| Integridad/anti-MITM| Validación de cert. de servidor + SAN  | `gen_certs.sh`, firmware |
| Autorización        | ACL por usuario (mínimo privilegio)    | `aclfile` |

---

## 5.2 — análisis de vulnerabilidades y mitigaciones

### V1 — (CRÍTICA) Llaves privadas versionadas en el repositorio

**Descripción.** Las llaves privadas de la PKI están **incluidas en el control de versiones** (git):

```
mosquitto/certs/ca.key          ← llave privada de la CA
mosquitto/certs/server.key      ← llave privada del servidor
nodered_data/certs/server.key   ← copia de la llave del servidor
```

El archivo [`.gitignore`](../.gitignore) **no** las excluye, por lo que fueron confirmadas (`git ls-files` las lista). Toda la seguridad TLS del sistema (5.1.2) descansa en el secreto de estas llaves; si se filtran, el cifrado deja de proteger.

**Impacto.**
- Con **`ca.key`**, un atacante puede **emitir certificados válidos** para *cualquier* servidor y montar un **ataque Man-in-the-Middle (MITM)**: se hace pasar por el broker, el ESP32 valida el certificado falso contra la CA legítima (porque está firmado por la CA cuya llave se filtró) y **entrega usuario y contraseña MQTT en claro** al atacante. A partir de ahí, el atacante descifra y **falsifica** telemetría y comandos (encender/apagar actuadores, inyectar lecturas de gas falsas, etc.).
- Con **`server.key`** más una captura de tráfico, se puede descifrar el tráfico TLS previamente grabado.
- Como el repositorio guarda el **historial**, borrar el archivo en un commit nuevo **no** elimina la llave: sigue siendo recuperable del historial de git.

**Escenario concreto.** El repositorio se publica en GitHub (o se comparte para la entrega). Un tercero clona, extrae `ca.key`, genera un `server.crt` propio para `CN=192.168.137.1`, se posiciona en la misma red (Wi-Fi del hogar / hotspot `192.168.137.x`) y responde a la conexión del ESP32. El dispositivo confía, autentica, y el atacante obtiene control total del canal MQTT — anulando de un golpe la autenticación (5.1.1) y el cifrado (5.1.2).

**Clasificación:** exposición de material criptográfico sensible (CWE-798 / CWE-321, *Hard-coded / Exposed Cryptographic Key*). Severidad **crítica**.

**Mitigación.**
1. **Excluir las llaves del repositorio.** Añadir a `.gitignore`:
   ```gitignore
   # Llaves privadas y material criptográfico — NUNCA versionar
   **/*.key
   mosquitto/certs/ca.key
   mosquitto/certs/server.key
   nodered_data/certs/
   ```
2. **Purgar el historial** con `git filter-repo` (o BFG Repo-Cleaner) para eliminar las llaves de *todos* los commits, no solo del último.
3. **Rotar (regenerar) toda la PKI**, ya que las llaves actuales deben considerarse comprometidas: volver a ejecutar `gen_certs.sh`, redistribuir el nuevo `ca.crt` a los clientes y descartar el material anterior.
4. **Versionar solo lo público y un ejemplo:** dejar en el repo `ca.crt` (público) y un `gen_certs.sh` reproducible; las llaves se generan localmente en cada despliegue y nunca se suben.
5. **Permisos del archivo** en el host: `chmod 600` sobre `*.key` para que solo el propietario las lea.

---

### V2 — (ALTA) Credenciales débiles y hardcodeadas en el firmware

**Descripción.** En [`codigo_ESP32.txt`](../codigo_ESP32.txt) las credenciales están escritas en claro y son triviales:

```cpp
const char* password  = "password123";   // Wi-Fi
const char* mqtt_pass = "zapato123";      // MQTT usuario esp32
```

**Impacto.** Como el firmware convive con la PKI en el mismo repositorio (V1), la filtración expone también las contraseñas. Además son **débiles** y vulnerables a diccionario/fuerza bruta si el broker quedara alcanzable. Con la contraseña de `esp32`, un atacante publica telemetría falsa dentro de los límites de su ACL.

**Mitigación.**
- Usar contraseñas **largas y aleatorias** (≥20 caracteres) por dispositivo, distintas entre roles.
- **No** versionar el firmware con las credenciales: externalizarlas (archivo `secrets.h` en `.gitignore`, o provisión por NVS/`Preferences` del ESP32).
- Rotar las credenciales actuales, dado que ya estuvieron expuestas.

---

### V3 — (MEDIA) TLS solo de servidor: sin autenticación mutua del dispositivo

**Descripción.** `require_certificate false` (5.1.2): el broker cifra y prueba su identidad, pero **no verifica criptográficamente** al dispositivo — la única prueba de que "es el ESP32" es la contraseña MQTT, reutilizable si se filtra.

**Impacto.** Un atacante con las credenciales (ver V1/V2) puede conectarse desde *cualquier* equipo haciéndose pasar por el ESP32; el broker no distingue el hardware.

**Mitigación.** Migrar a **TLS mutuo (mTLS)**: `require_certificate true` en `mosquitto.conf` y emitir un certificado de cliente por dispositivo (cargado en el ESP32 con `setCertificate`/`setPrivateKey`). Así, además de la contraseña, el dispositivo debe poseer una llave privada única — mucho más difícil de suplantar.

---

### V4 — (MEDIA) Servicios de gestión sin autenticación expuestos en la red

**Descripción.** En [`docker-compose.yml`](../docker-compose.yml) se publican sin autenticación:
- **Node-RED** en `1880` (editor de flujos): sin `adminAuth` configurado, cualquiera en la red edita los flujos y ve las credenciales embebidas.
- **Servicio de reconocimiento facial** en `5000`: API abierta.

**Impacto.** El editor de Node-RED es una vía directa a **ejecución de código** (nodos `function`, `exec`) y a los secretos del sistema. Es una superficie de administración mucho más peligrosa que el propio MQTT.

**Mitigación.**
- Configurar `adminAuth` (usuario/clave + `bcrypt`) en `settings.js` de Node-RED y servir el editor por HTTPS.
- No exponer el puerto `1880`/`5000` en la interfaz pública; limitarlos a `127.0.0.1` o a la red interna de Docker, accediendo por túnel/VPN.
- Añadir autenticación (token) al servicio de reconocimiento.

---

### V5 — (BAJA) Imagen Docker con tag `latest`

**Descripción.** `eclipse-mosquitto:latest` en [`docker-compose.yml`](../docker-compose.yml) no fija versión.

**Impacto.** Despliegues no reproducibles y riesgo de traer una imagen con cambios inesperados; dificulta auditar qué versión (y qué CVEs) está corriendo.

**Mitigación.** Fijar una versión concreta (p. ej. `eclipse-mosquitto:2.0.20`) y actualizar de forma controlada revisando el changelog/CVEs.

---

## Conclusión

El stack implementa correctamente las **medidas base** que pide la Unidad 2 — autenticación obligatoria, cifrado TLS en 8883 con PKI propia y validación de servidor, y autorización por tópico con mínimo privilegio —, cerrando el canal MQTT frente a accesos anónimos y escuchas pasivas.

El análisis revela, sin embargo, que **el eslabón más débil no es el protocolo sino la gestión del secreto**: las llaves privadas versionadas (V1) anulan en la práctica toda la protección TLS si el repositorio se comparte, y las credenciales débiles hardcodeadas (V2) agravan el problema. La mitigación prioritaria es **sacar las llaves y credenciales del control de versiones, purgar el historial y rotar todo el material comprometido**, y a mediano plazo endurecer el sistema con TLS mutuo (V3) y autenticación en los servicios de gestión (V4).

---

### Anexo — Tabla resumen de vulnerabilidades

| ID | Vulnerabilidad | Severidad | Mitigación principal |
|----|----------------|-----------|----------------------|
| V1 | Llaves privadas (`ca.key`, `server.key`) en git | **Crítica** | Excluir de git, purgar historial, rotar PKI |
| V2 | Credenciales débiles hardcodeadas en firmware | Alta | Secretos fuera de git, contraseñas fuertes, rotar |
| V3 | TLS solo de servidor (sin mTLS) | Media | `require_certificate true` + cert por dispositivo |
| V4 | Node-RED / API facial sin autenticación | Media | `adminAuth`, no exponer puertos, HTTPS |
| V5 | Imagen Docker con tag `latest` | Baja | Fijar versión concreta |
