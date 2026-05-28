import os
import face_recognition
from flask import Flask, request, jsonify

app = Flask(__name__)

# rutas de almacenamiento dentro del contenedor
directorio_imagenes = "/tmp/imagenes"
directorio_caras_conocidas = "/app/caras_conocidas"

caras_conocidas_encodings = []
caras_conocidas_nombres = []

# funcion para extraer las matrices de puntos de las fotos base al iniciar
def cargar_caras():
    global caras_conocidas_encodings, caras_conocidas_nombres
    caras_conocidas_encodings = []
    caras_conocidas_nombres = []
    
    if not os.path.exists(directorio_caras_conocidas):
        return
        
    for archivo in os.listdir(directorio_caras_conocidas):
        if archivo.endswith(".jpg") or archivo.endswith(".png"):
            ruta = os.path.join(directorio_caras_conocidas, archivo)
            try:
                imagen = face_recognition.load_image_file(ruta)
                encodings = face_recognition.face_encodings(imagen)
                if len(encodings) > 0:
                    caras_conocidas_encodings.append(encodings[0])
                    nombre = os.path.splitext(archivo)[0]
                    caras_conocidas_nombres.append(nombre)
            except Exception as e:
                print(f"error al cargar {archivo}: {e}")

cargar_caras()

@app.route('/recargar', methods=['POST', 'GET'])
def recargar_caras():
    try:
        cargar_caras()
        return jsonify({"resultado": "recarga_exitosa", "total_caras": len(caras_conocidas_nombres)}), 200
    except Exception as e:
        return jsonify({"error": f"error al recargar: {str(e)}"}), 500

@app.route('/reconocer', methods=['POST'])
def reconocer_cara():
    datos = request.get_json()
    if not datos or 'nombre_archivo' not in datos:
        return jsonify({"error": "falta el parametro nombre_archivo"}), 400
        
    ruta_imagen = os.path.join(directorio_imagenes, datos['nombre_archivo'])
    
    if not os.path.exists(ruta_imagen):
        return jsonify({"error": "archivo no encontrado en el volumen"}), 404
        
    try:
        imagen_desconocida = face_recognition.load_image_file(ruta_imagen)
        encodings_desconocidos = face_recognition.face_encodings(imagen_desconocida)
    except Exception as e:
        return jsonify({"error": f"imagen corrupta o ilegible: {str(e)}"}), 400
    
    if len(encodings_desconocidos) == 0:
        return jsonify({"resultado": "no_hay_rostro_visible"}), 200
        
    encoding_desconocido = encodings_desconocidos[0]
    
    # el parametro tolerance define la rigurosidad de la comparacion matematica
    resultados = face_recognition.compare_faces(caras_conocidas_encodings, encoding_desconocido, tolerance=0.6)
    
    nombre_identificado = "desconocido"
    
    if True in resultados:
        indice_coincidencia = resultados.index(True)
        nombre_identificado = caras_conocidas_nombres[indice_coincidencia]
        
    return jsonify({"resultado": nombre_identificado}), 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)