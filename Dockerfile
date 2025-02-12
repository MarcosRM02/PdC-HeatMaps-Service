# # Usa una imagen base con compilador de C++
# FROM gcc:latest

# COPY . .


# # Instala las dependencias necesarias
# # RUN apt-get update && apt-get install -y \
# #     libopencv-dev \
# #     libeigen3-dev \
# #     ffmpeg \
# #     libhiredis-dev \
# #     && rm -rf /var/lib/apt/lists/*
# RUN mkdir -p ./bin && \
#     g++ -std=c++17 \
#     -I./include \
#     -I/usr/include/eigen3 \
#     -I/usr/include/opencv4 \
#     -I/usr/include/hiredis \
#     heatMapsParalelizados.cpp \
#     -o ./bin/paralelizado \
#     -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_videoio \
#     -L/usr/lib/x86_64-linux-gnu -lhiredis

# # Establece el directorio de trabajo
# WORKDIR /usr/src/app

# # Copia el código fuente y las carpetas necesarias al contenedor

# # Compila el código
# RUN g++ -std=c++17 -I./include -I/usr/include/eigen3 -I/usr/include/opencv4 -I/usr/include/hiredis \
#     -o ./bin/paralelizado heatMapsParalelizados.cpp \
#     -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_videoio -lhiredis

# # Comando para ejecutar el binario
# CMD ["./bin/paralelizado"]


# # # Copia el script de ejecución y le da permisos de ejecución
# # COPY run.sh .
# # RUN chmod +x run.sh

# # # Comando por defecto
# # CMD ["./run.sh"]




FROM gcc:latest

# Instala dependencias (si es necesario)
RUN apt-get update && apt-get install -y \
    libopencv-dev \
    libeigen3-dev \
    ffmpeg \
    libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*

# Establece el directorio de trabajo
WORKDIR /usr/src/app

# Copia todos los archivos del contexto al WORKDIR
COPY . .

# Compila y genera el ejecutable en ./bin
# RUN mkdir -p bin && \
#     g++ -std=c++17 \
#     -I./include \
#     -I/usr/include/eigen3 \
#     -I/usr/include/opencv4 \
#     -I/usr/include/hiredis \
#     heatMapsParalelizados.cpp \
#     -o ./bin/paralelizado \
#     -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_videoio \
#     -lhiredis && \
#     echo "Contenido de /usr/src/app/bin:" && ls -l bin

# Comando para ejecutar el binario
# CMD ["./bin/paralelizado"]

RUN chmod +x run.sh

CMD ["./run.sh"]
