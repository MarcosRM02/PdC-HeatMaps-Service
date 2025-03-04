#!/bin/bash

# Espera hasta que Redis esté disponible
until timeout 5 bash -c "echo > /dev/tcp/redis_container/6379"; do
  echo "Esperando a que Redis inicie..."
  sleep 5
done

echo "Redis está activo. Ejecutando el programa en C."

# Verifica si el binario ya está compilado
if [ ! -f "./bin/hm_PostgreSql" ]; then
    echo "El binario no existe. Compilando..."
    mkdir -p ./bin
    # g++ -std=c++17 \
    # -I./include \
    # -I/usr/include/eigen3 \
    # -I/usr/include/opencv4 \
    # -I/usr/include/hiredis \
    #  heatMapsParalelizados.cpp \
    # -o ./bin/paralelizado \
    # -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_videoio \
    # -lhiredis && \
    g++ -std=c++17 -I./include -I/usr/include/eigen3 -I/usr/include/opencv4 -I/usr/include/hiredis \
    -o ./bin/hm_PostgreSql hm_PostgreSQL.cpp \
    -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_videoio -lhiredis -lcurl -I/usr/include/postgresql -L/usr/lib -lpq && \
    echo "Compilación completada."

    # Meter una comprobacion de compilacion correcta.
else
    echo "El binario ya existe. Saltando la compilación."
fi

# Ejecuta el programa
echo "Ejecutando el programa..."

./bin/hm_PostgreSql

