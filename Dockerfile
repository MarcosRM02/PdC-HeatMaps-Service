FROM gcc:latest

# Instala dependencias necesarias
RUN apt-get update && apt-get install -y \
    git \
    make \
    cmake \
    pkg-config \
    ffmpeg \
    libopencv-dev \
    libeigen3-dev \
    libpq-dev \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Clona y compila Hiredis v1.1.0
RUN git clone --branch v1.1.0 https://github.com/redis/hiredis.git /tmp/hiredis \
    && cd /tmp/hiredis \
    && make && make install \
    && ldconfig \
    && rm -rf /tmp/hiredis

# Establece el directorio de trabajo
WORKDIR /usr/src/app

# Copia el código fuente y el archivo run.sh
COPY . .  

# Da permisos de ejecución al script de inicio
RUN chmod +x run.sh

# Usa el script de ejecución como punto de entrada
CMD ["bash", "run.sh"]
