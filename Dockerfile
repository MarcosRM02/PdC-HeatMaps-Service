# Usa una imagen base con compilador de C++
FROM gcc:latest

# Instala las dependencias necesarias
RUN apt-get update && apt-get install -y \
    libopencv-dev \
    libeigen3-dev \
    ffmpeg \
    && rm -rf /var/lib/apt/lists/*

# Establece el directorio de trabajo
WORKDIR /usr/src/app

# Copia el código fuente y las carpetas necesarias al contenedor
COPY . .

# Copia el script de ejecución y le da permisos de ejecución
COPY run.sh .
RUN chmod +x run.sh

# Comando por defecto
CMD ["./run.sh"]
