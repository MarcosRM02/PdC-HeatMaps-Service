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

void printDF(const vector<vector<double>> &vec)
{
    for (const auto &row : vec)
    {
        for (const auto &elem : row)
        {
            std::cout << elem << " ";
        }
        std::cout << std::endl;
    }
}

void displayCoordinates(const std::vector<std::pair<double, double>> &coordinates_left)
{
    for (const auto &coord : coordinates_left)
    {
        std::cout << "(" << coord.first << ", " << coord.second << ")" << std::endl;
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

void mostrarMapeo(vector<DataMap> &coordinate_data)
{
    // Mostrar los resultados
    for (const auto &data : coordinate_data)
    {
        const auto &coord = data.coordinates;
        const auto &values = data.values;

        cout << "Coordenada (" << coord.first << ", " << coord.second << "): ";
        for (double value : values)
        {
            cout << value << " ";
        }
        cout << endl;
    }
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
    // displayCoordinates(coordinates);
    readCSVColumns(filename, coordinate_data);
    // Mostrar los resultados
    mostrarMapeo(coordinate_data);
}

void readCSVEncapsulation(const string &filename_L, vector<pair<double, double>> &coordinate_L, const string &filename_R, vector<pair<double, double>> &coordinate_R)
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
void visualize_heatmap(Mat &heatmap, const vector<pair<double, double>> &coordinates)
{
    // Definir colores contrastantes
    Scalar circle_color(255, 255, 255); // Blanco para los círculos
    Scalar text_color(255, 255, 255);   // Blanco para el texto
    Scalar text_outline_color(0, 0, 0); // Negro para el contorno del texto

    for (size_t i = 0; i < coordinates.size(); ++i)
    {
        // Convertir las coordenadas a enteros
        Point point(static_cast<int>(coordinates[i].first), static_cast<int>(coordinates[i].second));

        // Dibujar un círculo blanco en la posición
        circle(heatmap, point, 5, circle_color, -1);

        // Posición desplazada para el texto
        Point text_position = point + Point(5, 5);

        // Dibujar contorno negro para el texto (primero dibujar varias veces ligeramente desplazado)
        putText(heatmap, to_string(i), text_position, FONT_HERSHEY_SIMPLEX, 0.4, text_outline_color, 1, LINE_AA);
        putText(heatmap, to_string(i), text_position, FONT_HERSHEY_SIMPLEX, 0.4, text_outline_color, 1, LINE_AA);

        // Dibujar el texto blanco encima del contorno
        putText(heatmap, to_string(i), text_position, FONT_HERSHEY_SIMPLEX, 0.4, text_color, 1, LINE_AA);
    }
}

// -----------------------------------------------------------------

// // Función para generar la animación combinada
// void generate_combined_animation(const vector<Mat> &frames_left, const vector<Mat> &frames_right, const string &filename, double fps = 50.0)
// {
//     if (frames_left.empty() || frames_right.empty())
//     {
//         cerr << "No hay cuadros para guardar." << endl;
//         return;
//     }

//     int width = frames_left[0].cols + frames_right[0].cols; // Ancho combinado de los dos mapas
//     int height = frames_left[0].rows;                       // La altura es la misma para ambos

//     VideoWriter writer(filename, VideoWriter::fourcc('m', 'p', '4', 'v'), fps, Size(width, height));

//     for (size_t i = 0; i < frames_left.size(); ++i)
//     {
//         // Combinar los dos mapas de calor en una sola imagen
//         Mat combined_frame;
//         hconcat(frames_left[i], frames_right[i], combined_frame); // Concatenar los mapas de calor horizontalmente

//         Mat color_frame;
//         // applyColorMap(combined_frame / 16, color_frame, COLORMAP_JET); // Aplicar un mapa de colores
//         Mat combined_frame_8UC1;
//         combined_frame.convertTo(combined_frame_8UC1, CV_8UC1, 255.0 / 4095.0); // Escalar a 8 bits
//         applyColorMap(combined_frame_8UC1, color_frame, COLORMAP_JET);

//         writer.write(color_frame); // Escribir el cuadro en el archivo de video
//     }

//     writer.release();
//     cout << "Animación combinada guardada como " << filename << endl;
// }

// Función del hilo que escribe los frames en ffmpeg
void writer_thread_function(const string &filename, int width, int height, double fps)
{

    // Construir el comando de ffmpeg

    string cmd = "ffmpeg -y -threads 8 -f rawvideo -pixel_format bgr24 -video_size " +

                 to_string(width) + "x" + to_string(height) +

                 " -framerate " + to_string(fps) +

                 " -i pipe:0 -c:v libx264 -preset fast -crf 23 -x264-params \"threads=8\" " +

                 "-pix_fmt yuv420p \"" + filename + "\"";

    // Abrir el pipe a ffmpeg

    FILE *ffmpeg_pipe = popen(cmd.c_str(), "w");

    if (!ffmpeg_pipe)
    {

        cerr << "Error: No se pudo iniciar ffmpeg." << endl;

        return;
    }

    // Leer frames de la cola y escribirlos

    while (true)
    {

        Mat frame;

        {

            unique_lock<mutex> lock(queue_mutex);

            queue_cond.wait(lock, []
                            { return !frame_queue.empty() || processing_done; });

            if (processing_done && frame_queue.empty())
                break;

            if (!frame_queue.empty())
            {

                frame = frame_queue.front();

                frame_queue.pop();
            }
        }

        if (!frame.empty())
        {

            // Escribir el frame en formato rawvideo

            size_t bytes_written = fwrite(frame.data, 1, frame.total() * frame.elemSize(), ffmpeg_pipe);

            if (bytes_written != frame.total() * frame.elemSize())
            {

                cerr << "Error al escribir el frame." << endl;
            }
        }
    }

    // Cerrar el pipe

    pclose(ffmpeg_pipe);
}

void generate_combined_animation(const vector<Mat> &frames_left, const vector<Mat> &frames_right,

                                 const string &filename, double fps = 50.0)
{

    if (frames_left.empty() || frames_right.empty())
    {

        cerr << "No hay cuadros para guardar." << endl;

        return;
    }

    int width = frames_left[0].cols + frames_right[0].cols;

    int height = frames_left[0].rows;

    // Iniciar hilo escritor

    processing_done = false;

    thread writer_thread(writer_thread_function, filename, width, height, fps);

    // Procesar cada frame y añadirlo a la cola

    for (size_t i = 0; i < frames_left.size(); ++i)
    {

        Mat combined_frame;

        hconcat(frames_left[i], frames_right[i], combined_frame);

        Mat combined_frame_8UC1;

        combined_frame.convertTo(combined_frame_8UC1, CV_8UC1, 255.0 / 4095.0);

        Mat color_frame;

        applyColorMap(combined_frame_8UC1, color_frame, COLORMAP_JET);

        // Añadir frame a la cola de forma segura

        {

            lock_guard<mutex> lock(queue_mutex);

            frame_queue.push(color_frame.clone()); // Clonar para evitar aliasing
        }

        queue_cond.notify_one();
    }

    // Señalizar fin del procesamiento

    {

        lock_guard<mutex> lock(queue_mutex);

        processing_done = true;
    }

    queue_cond.notify_one();

    // Esperar a que el hilo escritor termine

    writer_thread.join();

    cout << "Animación combinada guardada como " << filename << endl;
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
int main()
{
    // Parámetros iniciales
    // int width = 350, height = 1040;
    int width = 175, height = 520;
    vector<pair<double, double>> coordinates_left;  // Ejemplo de coordenadas
    vector<pair<double, double>> coordinates_right; // Ejemplo de coordenadas
    readCSVEncapsulation("leftPoints.json", coordinates_left, "rightPoints.json", coordinates_right);
    vector<vector<double>> pressures_left = read_csv("left.csv");   // Ejemplo de presiones
    vector<vector<double>> pressures_right = read_csv("right.csv"); // Ejemplo de presiones

    vector<Mat> frames_left, frames_right;

    std::thread right([&]()
                      { usingThreads(width, height, pressures_right, coordinates_right, frames_right); }); // spawn new thread that calls usingThreads for the right side

    std::thread left([&]()
                     { usingThreads(width, height, pressures_left, coordinates_left, frames_left); }); // spawn new thread that calls usingThreads for the left side

    // synchronize threads:
    right.join(); // pauses until first finishes
    left.join();  // pauses until second finishes

    // Generar la animación combinada
    generate_combined_animation(frames_left, frames_right, "combined_pressure_animation_paralelizado.mp4", 50.0);

    return 0;
}
