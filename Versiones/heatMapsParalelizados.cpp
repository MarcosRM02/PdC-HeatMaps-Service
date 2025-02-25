#include <iostream>
#include <fstream>
#include <vector>
#include <sstream> // Agregado para std::stringstream
#include <opencv2/opencv.hpp>
#include <Eigen/Dense>
#include <cmath>
#include <rapidjson/document.h> // Incluir RapidJSON
#include <utility>              // for std::pair
#include <map>                  // optional, for better association
#include <thread>               // std::thread

#include <mutex>
#include <queue>
#include <condition_variable>
#include <cstdio>

#include <hiredis/hiredis.h> // Incluir hiredis para Redis

using namespace std;
using namespace Eigen;
using namespace rapidjson;
using namespace cv;

//-------------------
// Variables para sincronización entre hilos

queue<Mat> frame_queue;
mutex queue_mutex;
condition_variable queue_cond;
bool processing_done = false;

//----------------------

struct DataMap
{
    pair<double, double> coordinates;
    vector<double> values;
};

// Función para leer las coordenadas desde un archivo JSON utilizando RapidJSON
void read_coordinates(const string &filename, vector<pair<double, double>> &coordinates)
{
    // Abrir el archivo JSON
    ifstream file(filename);
    if (!file.is_open())
    {
        cerr << "No se pudo abrir el archivo JSON: " << filename << endl;
        return;
    }

    stringstream buffer;
    buffer << file.rdbuf(); // Leer todo el archivo JSON
    string json_str = buffer.str();

    // Crear un documento RapidJSON e interpretar el JSON
    Document document;
    document.Parse(json_str.c_str());

    // Verificar si el JSON es válido
    if (document.HasParseError())
    {
        cerr << "Error al analizar el archivo JSON: " << filename << endl;
        return;
    }

    // Verificar que el JSON sea un arreglo
    if (!document.IsArray())
    {
        cerr << "El archivo JSON no contiene un arreglo: " << filename << endl;
        return;
    }

    // Leer las coordenadas del JSON
    for (SizeType i = 0; i < document.Size(); ++i)
    {
        if (document[i].HasMember("x") && document[i].HasMember("y") &&
            document[i]["x"].IsDouble() && document[i]["y"].IsDouble())
        {
            double x = document[i]["x"].GetDouble();
            double y = document[i]["y"].GetDouble();
            coordinates.emplace_back(x, y);
        }
        else
        {
            cerr << "Coordenada inválida en el índice " << i << " del archivo JSON: " << filename << endl;
        }
    }
}

// ------------------------------------------------------------------------------------------

void readCSVColumns(const string &filename, vector<DataMap> &coordinate)
{
    // Abre el archivo CSV
    ifstream file(filename);
    string line;

    if (!file.is_open())
    {
        cerr << "No se pudo abrir el archivo." << endl;
    }

    // Ignorar la primera línea (cabeceras)
    getline(file, line);

    // Leer el archivo línea por línea
    while (getline(file, line))
    {
        stringstream ss(line);
        string value;
        size_t col_idx = 0;

        // Separar valores por coma y asociarlos al vector correspondiente
        while (getline(ss, value, ','))
        {
            if (col_idx < coordinate.size())
            {
                coordinate[col_idx].values.push_back(stod(value)); // Almacenar en la estructura correspondiente
            }
            col_idx++;
        }
    }

    file.close();
}

void inicializarDataMaps(vector<pair<double, double>> &coordinates, vector<DataMap> &coordinateMap)
{
    for (size_t i = 0; i < coordinates.size(); ++i)
    {
        coordinateMap[i].coordinates = coordinates[i];
    }
}

void threadFunction(const string &filename, vector<pair<double, double>> &coordinates, vector<DataMap> &coordinate_data)
{
    inicializarDataMaps(coordinates, coordinate_data);
    readCSVColumns(filename, coordinate_data);
}

void readCoordinatesEncapsulation(const string &filename_L, vector<pair<double, double>> &coordinate_L, const string &filename_R, vector<pair<double, double>> &coordinate_R)
{
    read_coordinates(filename_L, coordinate_L);
    read_coordinates(filename_R, coordinate_R);

    if (coordinate_L.empty() || coordinate_R.empty())
    {
        cerr << "Error: Las coordenadas no se pudieron leer correctamente desde los archivos JSON." << endl;
    }
}

// Función para generar el mapa de calor circular
Mat circular_heat_map(const vector<pair<double, double>> &coordinates, const vector<double> &pressures, int width, int height, double radius = 70.0, double smoothness = 2.0)
{
    Mat heatmap = Mat::zeros(height, width, CV_64F); // Matriz para almacenar el mapa de calor
    for (size_t i = 0; i < coordinates.size(); ++i)
    {
        double x0 = coordinates[i].first;
        double y0 = coordinates[i].second;
        double p0 = pressures[i];

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                double dist = sqrt(pow(x - x0, 2) + pow(y - y0, 2));
                heatmap.at<double>(y, x) += p0 * exp(-smoothness * pow(dist / radius, 2));
            }
        }
    }

    normalize(heatmap, heatmap, 0, 4095, NORM_MINMAX); // Normalizar los valores entre 0 y 4095
    return heatmap;
}

// Función para visualizar los mapas de calor y los puntos
void visualize_heatmap(Mat &image, const vector<pair<double, double>> &coordinates)
{
    // Colores: círculo en negro, texto en blanco
    Scalar circle_color(0, 0, 0);     // Negro para los círculos
    Scalar text_color(255, 255, 255); // Blanco para el texto

    for (size_t i = 0; i < coordinates.size(); ++i)
    {
        // Convertir las coordenadas a enteros
        Point point(static_cast<int>(coordinates[i].first), static_cast<int>(coordinates[i].second));

        // Dibujar un círculo negro en la posición
        circle(image, point, 5, circle_color, -1);

        // Posición desplazada para el texto
        Point text_position = point + Point(5, 5);

        // Dibujar el texto blanco sin contorno
        putText(image, to_string(i), text_position, FONT_HERSHEY_SIMPLEX, 0.4, text_color, 1, LINE_AA);
    }
}

//__________________________________________________________________________________________________
vector<vector<double>> read_csv(const string &filename)
{
    vector<vector<double>> data;
    ifstream file(filename);
    string line;

    while (getline(file, line))
    {
        vector<double> row;
        stringstream ss(line);
        string value;

        while (getline(ss, value, ','))
        {
            try
            {
                row.push_back(stod(value));
            }
            catch (const invalid_argument &e)
            {
                cerr << "Valor inválido en el archivo CSV: " << value << endl;
                row.push_back(0.0); // O manejar el error según tus necesidades
            }
        }
        data.push_back(row);
    }

    return data;
}

void usingThreads(int &width, int &height, vector<vector<double>> &pressures, vector<pair<double, double>> &coordinates, vector<Mat> &frames)
{
    // Generar los mapas de calor para cada cuadro
    for (size_t frame = 0; frame < pressures.size(); ++frame)
    {
        Mat heatmap = circular_heat_map(coordinates, pressures[frame], width, height);
        // Visualizar los puntos sobre el mapa º calor
        visualize_heatmap(heatmap, coordinates);

        frames.push_back(heatmap);
    }
}

// Función para convertir una imagen en escala de grises (CV_8UC1) a una imagen a color usando un mapeo personalizado
Mat applyCustomColorMap(const Mat &grayImage)
{
    // Definir los puntos de control y sus colores (en formato hexadecimal convertido a BGR)
    // Los colores se definen de la siguiente manera:
    // - "#0000FF" (azul): en BGR es (255, 0, 0)
    // - "#00FFFF" (cian): en BGR es (255, 255, 0)
    // - "#00FF00" (verde): en BGR es (0, 255, 0)
    // - "#FFFF00" (amarillo): en BGR es (0, 255, 255)
    // - "#FFA500" (naranja): en BGR es (0, 165, 255)
    // - "#FF4500" (rojo anaranjado): en BGR es (0, 69, 255)
    // - "#FF0000" (rojo): en BGR es (0, 0, 255)
    // - "#8B0000" (rojo oscuro): en BGR es (0, 0, 139)

    vector<int> stops = {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4095};
    vector<Vec3b> colors;
    colors.push_back(Vec3b(255, 0, 0));   // 0: "#0000FF"
    colors.push_back(Vec3b(255, 0, 0));   // 500: "#0000FF"
    colors.push_back(Vec3b(255, 255, 0)); // 1000: "#00FFFF"
    colors.push_back(Vec3b(0, 255, 0));   // 1500: "#00FF00"
    colors.push_back(Vec3b(0, 255, 255)); // 2000: "#FFFF00"
    colors.push_back(Vec3b(0, 165, 255)); // 2500: "#FFA500"
    colors.push_back(Vec3b(0, 69, 255));  // 3000: "#FF4500"
    colors.push_back(Vec3b(0, 0, 255));   // 3500: "#FF0000"
    colors.push_back(Vec3b(0, 0, 139));   // 4000: "#8B0000"
    colors.push_back(Vec3b(0, 0, 139));   // 4095: "#8B0000"

    // Crear la imagen de salida (3 canales, 8 bits)
    Mat colorImage(grayImage.size(), CV_8UC3);

    // Recorrer cada píxel y asignar el color interpolado según el rango
    for (int i = 0; i < grayImage.rows; i++)
    {
        for (int j = 0; j < grayImage.cols; j++)
        {
            // Obtenemos el valor del píxel
            int value = static_cast<int>(grayImage.at<uchar>(i, j));
            // Convertir el valor (0-255) a la escala original (0-4095)
            double scaledValue = value * (4095.0 / 255.0);

            Vec3b color;
            if (scaledValue <= stops.front())
            {
                color = colors.front();
            }
            else if (scaledValue >= stops.back())
            {
                color = colors.back();
            }
            else
            {
                // Buscar entre qué dos paradas se encuentra el valor
                for (size_t k = 0; k < stops.size() - 1; k++)
                {
                    if (scaledValue >= stops[k] && scaledValue < stops[k + 1])
                    {
                        // Interpolación lineal entre colors[k] y colors[k+1]
                        double t = (scaledValue - stops[k]) / static_cast<double>(stops[k + 1] - stops[k]);
                        color[0] = static_cast<uchar>((1 - t) * colors[k][0] + t * colors[k + 1][0]);
                        color[1] = static_cast<uchar>((1 - t) * colors[k][1] + t * colors[k + 1][1]);
                        color[2] = static_cast<uchar>((1 - t) * colors[k][2] + t * colors[k + 1][2]);
                        break;
                    }
                }
            }
            colorImage.at<Vec3b>(i, j) = color;
        }
    }
    return colorImage;
}

void generate_combined_animation_sequential(
    const vector<Mat> &frames_left,
    const vector<Mat> &frames_right,
    const string &filename,
    vector<pair<double, double>> &coordinates_left,
    vector<pair<double, double>> &coordinates_right,
    double fps = 50.0)
{
    if (frames_left.empty() || frames_right.empty())
    {
        cerr << "No hay cuadros para guardar." << endl;
        return;
    }

    // Resolución combinada (se mantiene igual para que se vean los sensores)
    int width = frames_left[0].cols + frames_right[0].cols;
    int height = frames_left[0].rows;

    // Construir el comando para FFmpeg (ajustando parámetros para reducir calidad)
    string cmd = "ffmpeg -y -threads 1 -f rawvideo -pixel_format bgr24 -video_size " +
                 to_string(width) + "x" + to_string(height) +
                 " -framerate " + to_string(fps) +
                 " -i pipe:0 -c:v libx264 -preset fast -crf 28 -b:v 500k -pix_fmt yuv420p \"" + filename + "\"";

    // Abrir el pipe hacia FFmpeg
    FILE *ffmpeg_pipe = popen(cmd.c_str(), "w");
    if (!ffmpeg_pipe)
    {
        cerr << "Error: No se pudo iniciar FFmpeg." << endl;
        return;
    }

    ofstream dataFile("animation_data_log.txt");
    if (!dataFile.is_open())
    {
        cerr << "No se pudo abrir el archivo de datos." << endl;
    }

    // Procesar cada frame secuencialmente para mantener el orden
    for (size_t i = 0; i < frames_left.size(); ++i)
    {
        dataFile << "Frame " << i << ":\n";

        // Datos de los sensores del lado izquierdo
        dataFile << "  Izquierda:\n";
        for (size_t j = 0; j < coordinates_left.size(); ++j)
        {
            // Se asume que pressures_left[i] tiene al menos coordinates_left.size() elementos
            dataFile << "    Sensor " << j
                     << " - Coordenadas: (" << coordinates_left[j].first << ", " << coordinates_left[j].second << ")"
                     << " | Presión: " << frames_left[i] << "\n";
        }

        // Datos de los sensores del lado derecho
        dataFile << "  Derecha:\n";
        for (size_t j = 0; j < coordinates_right.size(); ++j)
        {
            // Se asume que pressures_right[i] tiene al menos coordinates_right.size() elementos
            dataFile << "    Sensor " << j
                     << " - Coordenadas: (" << coordinates_right[j].first << ", " << coordinates_right[j].second << ")"
                     << " | Presión: " << frames_right[i] << "\n";
        }
        dataFile << "\n";
        // Combinar horizontalmente el frame izquierdo y derecho
        Mat combined_frame;
        hconcat(frames_left[i], frames_right[i], combined_frame);

        // Convertir a 8 bits (necesario para aplicar el colormap)
        Mat combined_frame_8UC1;
        combined_frame.convertTo(combined_frame_8UC1, CV_8UC1, 255.0 / 4095.0);

        // Aplicar el colormap personalizado
        Mat color_frame = applyCustomColorMap(combined_frame_8UC1);

        // Dibujar los sensores en la parte izquierda
        visualize_heatmap(color_frame, coordinates_left);

        // Ajustar las coordenadas para la parte derecha sumando el ancho del frame izquierdo
        vector<pair<double, double>> coordinates_right_offset;
        for (const auto &pt : coordinates_right)
        {
            coordinates_right_offset.emplace_back(pt.first + frames_left[0].cols, pt.second);
        }
        visualize_heatmap(color_frame, coordinates_right_offset);

        // Aplicar el efecto de pixelación:
        double scaleFactor = 0.05; // Ajusta este valor para modificar el grado de pixelación
        Mat reduced;
        resize(color_frame, reduced, Size(), scaleFactor, scaleFactor, INTER_NEAREST);
        Mat pixelated;
        resize(reduced, pixelated, color_frame.size(), 0, 0, INTER_NEAREST);

        // Escribir el frame directamente en el pipe de FFmpeg
        size_t bytes_written = fwrite(color_frame.data, 1, color_frame.total() * color_frame.elemSize(), ffmpeg_pipe);
        if (bytes_written != color_frame.total() * color_frame.elemSize())
        {
            cerr << "Error al escribir el frame " << i << endl;
        }
    }
    dataFile.close();

    // Cerrar el pipe para finalizar la escritura del video
    pclose(ffmpeg_pipe);
    cout << "Animación combinada guardada como " << filename << endl;
}

int main()
{
    // Parámetros iniciales
    // int width = 350, height = 1040;
    int width = 175, height = 520;

    vector<pair<double, double>> coordinates_left;                                                            // Ejemplo de coordenadas
    vector<pair<double, double>> coordinates_right;                                                           // Ejemplo de coordenadas
    readCoordinatesEncapsulation("leftPoints.json", coordinates_left, "rightPoints.json", coordinates_right); // Revisar esto, pq estoyu

    vector<vector<double>> pressures_left = read_csv("left.csv");   // Ejemplo de presiones
    vector<vector<double>> pressures_right = read_csv("right.csv"); // Ejemplo de presiones

    // vector<vector<double>> pressures_left;  // Ejemplo de presiones
    // vector<vector<double>> pressures_right; // Ejemplo de presiones
    // readFromRedisDBEncapsulation(pressures_left, pressures_right);

    vector<Mat> frames_left, frames_right;

    std::thread right([&]()
                      { usingThreads(width, height, pressures_right, coordinates_right, frames_right); }); // spawn new thread that calls usingThreads for the right side

    std::thread left([&]()
                     { usingThreads(width, height, pressures_left, coordinates_left, frames_left); }); // spawn new thread that calls usingThreads for the left side

    // synchronize threads:
    right.join(); // pauses until first finishes
    left.join();  // pauses until second finishes

    // Generar la animación combinada
    // generate_combined_animation(frames_left, frames_right, "combined_pressure_animation_paralelizado.mp4", coordinates_left, coordinates_right, 50.0);
    generate_combined_animation_sequential(frames_left, frames_right, "combined_pressure_animation_paralelizado.mp4", coordinates_left, coordinates_right, 50);
    // Definición de la variable global
    // const std::string queue = "redis_queue";

    // consumeFromQueue(queue);

    return 0;
}
