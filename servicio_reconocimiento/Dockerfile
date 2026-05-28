FROM python:3.9-slim AS builder

# herramientas necesarias para compilar dlib y dependencias de C++
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libopenblas-dev \
    liblapack-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY requirements.txt .

# instalamos dependencias en un directorio local para copiarlas facilmente
RUN pip install --no-cache-dir --user -r requirements.txt


FROM python:3.9-slim

# dependencias de ejecucion del sistema 
RUN apt-get update && apt-get install -y --no-install-recommends \
    libopenblas0 \
    liblapack3 \
    libx11-6 \
    libgtk-3-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /root/.local /root/.local
ENV PATH=/root/.local/bin:$PATH

COPY . .

EXPOSE 5000

CMD ["python", "app.py"]
