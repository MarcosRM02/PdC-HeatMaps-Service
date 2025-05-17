#!/bin/bash

echo "Esperando a que Redis esté disponible..."
until timeout 5 bash -c "echo > /dev/tcp/redis_container/6379"; do
  echo "Esperando a que Redis inicie..."
  sleep 5
done

# 2) Espera al backend
echo "Esperando a ssith-backend en el puerto 3000..."
until timeout 5 bash -c "echo > /dev/tcp/ssith-backend/3000"; do
  echo "  Backend no listo, reintentando..."
  sleep 5
done
echo "Backend activo."

echo "Redis está activo. Compilando y ejecutando el programa..."

# Compila el código en cada inicio del contenedor
mkdir -p ./bin
g++ -std=c++17 -I./include -I/usr/include/eigen3 -I/usr/include/opencv4 -I/usr/include/hiredis \
    -o ./bin/hm_Service hm_Service.cpp \
    -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_videoio -lhiredis -lcurl -I/usr/include/postgresql -L/usr/lib -lpq

echo "Compilación completada. Ejecutando..."
./bin/hm_Service
