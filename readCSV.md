
Para leer los csv:

### 1. **Descargar la biblioteca `csv-parser`**
La biblioteca `csv-parser` está disponible en GitHub. Sigue estos pasos para descargarla:

- Ve al repositorio de [CSV-parser](https://github.com/vincentlaucsb/csv-parser).
- Descarga el archivo ZIP del repositorio o clona el repositorio usando Git:
  ```bash
  git clone https://github.com/vincentlaucsb/csv-parser.git
  ```
- Copia el archivo `csv.hpp` y los demás archivos necesarios a una carpeta dentro de tu proyecto. Por ejemplo, puedes crear una carpeta llamada `include` y colocar el archivo allí.

---

### 2. **Actualizar el includePath en VS Code**
VS Code utiliza el archivo `c_cpp_properties.json` para configurar las rutas de inclusión. Sigue estos pasos:

1. Ve a **`View` > `Command Palette` > `C/C++: Edit Configurations (JSON)`**.
2. Localiza el campo `"includePath"`.
3. Añade la ruta a la carpeta donde colocaste `csv.hpp`. Por ejemplo, si la carpeta `include` está en el directorio raíz de tu proyecto:
   ```json
   "includePath": [
       "${workspaceFolder}/include",
       "${workspaceFolder}/**",
       "/usr/include",
       "/usr/local/include"
   ]
   ```

---

### 3. **Verifica la configuración de tu proyecto**
Si estás usando un compilador como `g++`, asegúrate de especificar la ruta de inclusión al compilar. Por ejemplo:

```bash
g++ -I./include -o heatMaps heatMaps.cpp
```

Aquí `-I./include` le dice al compilador que busque archivos de encabezado en la carpeta `include`.

---

### 4. **Verificar dependencias adicionales**
La biblioteca `csv-parser` puede requerir que instales ciertas dependencias, como `libstdc++fs` (soporte para `std::filesystem`). Si encuentras errores relacionados con esto, puedes solucionarlo añadiendo la bandera `-lstdc++fs` al compilar:

```bash
g++ -I./include -o heatMaps heatMaps.cpp -lstdc++fs
```

---

### 5. **Usar la biblioteca en tu código**
Después de configurar correctamente tu entorno, deberías poder incluir `csv.hpp` sin problemas:

```cpp
#include "csv.hpp"
```

# Leer JSON:

Leer un archivo JSON en C++ se puede realizar utilizando bibliotecas especializadas que manejan este formato de datos. Algunas de las bibliotecas más populares para trabajar con JSON en C++ son:

1. **[nlohmann/json](https://github.com/nlohmann/json)**: Una biblioteca moderna, fácil de usar, y ampliamente adoptada.
2. **[RapidJSON](https://rapidjson.org/)**: Muy eficiente y orientada al rendimiento.
3. **[jsoncpp](https://github.com/open-source-parsers/jsoncpp)**: Más básica, pero funcional.

A continuación, te muestro cómo leer un archivo JSON utilizando **nlohmann/json**, que es una de las opciones más amigables para empezar.

---

### 1. **Configura nlohmann/json**
#### Instalación con `vcpkg` (opción recomendada):
Si usas `vcpkg` para gestionar paquetes en tu proyecto, puedes instalar la biblioteca así:
```bash
vcpkg install nlohmann-json
```

#### Manual:
Descarga el archivo [json.hpp](https://github.com/nlohmann/json/releases) y colócalo en tu carpeta de inclusión del proyecto (por ejemplo, en una carpeta `include/`).

---

### 2. **Ejemplo de código: Leer un archivo JSON**
Supongamos que tienes un archivo `datos.json` con este contenido:

```json
{
    "nombre": "Marcos",
    "edad": 30,
    "habilidades": ["C++", "Python", "JavaScript"]
}
```

Puedes leer este archivo y procesarlo con el siguiente código:

#### Código C++:
```cpp
#include <iostream>
#include <fstream>  // Para leer archivos
#include <nlohmann/json.hpp> // Incluye la biblioteca nlohmann/json

using json = nlohmann::json;

int main() {
    // Abrir el archivo JSON
    std::ifstream archivo("datos.json");
    if (!archivo.is_open()) {
        std::cerr << "No se pudo abrir el archivo JSON." << std::endl;
        return 1;
    }

    // Leer el contenido del archivo en un objeto JSON
    json datos;
    archivo >> datos;

    // Acceder a los datos
    std::string nombre = datos["nombre"];
    int edad = datos["edad"];
    std::vector<std::string> habilidades = datos["habilidades"];

    // Mostrar los datos
    std::cout << "Nombre: " << nombre << std::endl;
    std::cout << "Edad: " << edad << std::endl;
    std::cout << "Habilidades: ";
    for (const auto& habilidad : habilidades) {
        std::cout << habilidad << " ";
    }
    std::cout << std::endl;

    return 0;
}
```

---

### 3. **Explicación del código**
1. **`#include <nlohmann/json.hpp>`**: Incluye la biblioteca JSON.
2. **`std::ifstream`**: Abre el archivo JSON para lectura.
3. **`json datos;`**: Crea un objeto JSON donde se almacenarán los datos del archivo.
4. **`archivo >> datos;`**: Convierte el contenido del archivo JSON en un objeto JSON.
5. **Acceso a datos**: Puedes acceder a los elementos del JSON como si fuera un mapa (`map`) o una lista.

---

### 4. **Compilación**
Si usas `g++`, incluye la ruta de `json.hpp` y compila:

```bash
g++ -I./include main.cpp -o main
```

Si instalaste con `vcpkg`, añade las rutas necesarias al compilar:
```bash
g++ main.cpp -o main -I/path/to/vcpkg/installed/x64-linux/include
```

---

### Bibliotecas alternativas
- **RapidJSON**: Muy rápida, pero un poco más compleja de usar.
- **jsoncpp**: Ligera, pero menos moderna y con características más limitadas.