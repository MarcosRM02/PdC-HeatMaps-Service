#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <thread>
#include <cstdio>    // popen, pclose
#include <algorithm> // std::min
#include <mutex>

#include <opencv2/opencv.hpp>

// RapidJSON (asegúrate de tener los headers en el include path)
#include <rapidjson/document.h>

using namespace std;
using namespace cv;
using namespace rapidjson;

//-------------------------------------------------------------------
// Estructura para asociar (x,y) + presiones
//-------------------------------------------------------------------
struct DataMap
{
    pair<double, double> coordinates; // (x,y)
    vector<double> values;            // presiones en varios frames
};

//-------------------------------------------------------------------
// 1) Función para leer coordenadas desde un archivo JSON con RapidJSON
//-------------------------------------------------------------------
void read_coordinates(const string &filename, vector<pair<double, double>> &coordinates)
{
    ifstream file(filename);
    if (!file.is_open())
    {
        cerr << "No se pudo abrir el archivo JSON: " << filename << endl;
        return;
    }

    stringstream buffer;
    buffer << file.rdbuf();
    string json_str = buffer.str();

    Document document;
    document.Parse(json_str.c_str());

    if (document.HasParseError())
    {
        cerr << "Error al analizar el archivo JSON: " << filename << endl;
        return;
    }

    if (!document.IsArray())
    {
        cerr << "El archivo JSON no contiene un arreglo: " << filename << endl;
        return;
    }

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

//-------------------------------------------------------------------
// 2) Función para leer CSV (cada fila = frame, columnas = sensores)
//    Devuelve vector< vector<double> >
//-------------------------------------------------------------------
vector<vector<double>> read_csv(const string &filename)
{
    vector<vector<double>> data;
    ifstream file(filename);
    if (!file.is_open())
    {
        cerr << "No se pudo abrir el archivo CSV: " << filename << endl;
        return data;
    }

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
            catch (...)
            {
                row.push_back(0.0);
            }
        }
        data.push_back(row);
    }
    return data;
}

//-------------------------------------------------------------------
// 3) Interpolación lineal de color (BGR)
//-------------------------------------------------------------------
static inline Vec3b interpolateColor(const Vec3b &c1, const Vec3b &c2, double t)
{
    Vec3b result;
    for (int i = 0; i < 3; i++)
    {
        result[i] = static_cast<uchar>((1.0 - t) * c1[i] + t * c2[i]);
    }
    return result;
}

//-------------------------------------------------------------------
// 4) Asignar un valor [0..4095] a un color BGR según stops
//-------------------------------------------------------------------
Vec3b getColorFromStops(int v,
                        const vector<int> &stops,
                        const vector<Vec3b> &stopColors)
{
    // Clamp:
    if (v < 0)
        v = 0;
    if (v > 4095)
        v = 4095;

    // Buscar el intervalo
    for (size_t i = 0; i < stops.size() - 1; i++)
    {
        int s1 = stops[i], s2 = stops[i + 1];
        if (v >= s1 && v <= s2)
        {
            double t = double(v - s1) / double(s2 - s1);
            return interpolateColor(stopColors[i], stopColors[i + 1], t);
        }
    }
    // Por si algo queda fuera:
    return stopColors.back();
}

//-------------------------------------------------------------------
// 5) Generar heatmap con grid "reducido" + upsample a (widthFinal x heightFinal)
//-------------------------------------------------------------------
Mat generateHeatMapFrameDownsample(
    const vector<pair<double, double>> &coords, // sensor coords
    const vector<double> &pressures,            // presiones para este frame
    int widthFinal, int heightFinal,            // resolución destino
    int gridWidth, int gridHeight,
    double radius, double smoothness,
    const vector<int> &stops,
    const vector<Vec3b> &stopColors)
{
    // 1) Matriz Z en resolución gridHeight x gridWidth
    vector<vector<double>> Z(gridHeight, vector<double>(gridWidth, 0.0));

    // 2) Rellenar Z con las gaussianas
    //    Xg(gx) = (gx+0.5)/gridWidth * widthFinal
    //    Yg(gy) = (gy+0.5)/gridHeight * heightFinal
    for (size_t i = 0; i < coords.size(); i++)
    {
        double x0 = coords[i].first;
        double y0 = coords[i].second;
        double p0 = pressures[i];

        for (int gy = 0; gy < gridHeight; gy++)
        {
            double Yg = (gy + 0.5) / double(gridHeight) * heightFinal;
            for (int gx = 0; gx < gridWidth; gx++)
            {
                double Xg = (gx + 0.5) / double(gridWidth) * widthFinal;
                double dx = Xg - x0;
                double dy = Yg - y0;
                double dist2 = dx * dx + dy * dy;
                double contribution = p0 * exp(-smoothness * (dist2 / (radius * radius)));
                Z[gy][gx] += contribution;
            }
        }
    }

    // 3) Clamp a [0..4095]
    for (int gy = 0; gy < gridHeight; gy++)
    {
        for (int gx = 0; gx < gridWidth; gx++)
        {
            if (Z[gy][gx] < 0.0)
                Z[gy][gx] = 0.0;
            if (Z[gy][gx] > 4095.0)
                Z[gy][gx] = 4095.0;
        }
    }

    // 4) Crear imagen BGR final
    Mat heatmap(heightFinal, widthFinal, CV_8UC3, Scalar(0, 0, 0));

    // 5) Upsample (x,y) -> (gx,gy)
    for (int y = 0; y < heightFinal; y++)
    {
        int gy = int(double(y) / heightFinal * gridHeight);
        if (gy < 0)
            gy = 0;
        if (gy >= gridHeight)
            gy = gridHeight - 1;

        uchar *rowPtr = heatmap.ptr<uchar>(y);

        for (int x = 0; x < widthFinal; x++)
        {
            int gx = int(double(x) / widthFinal * gridWidth);
            if (gx < 0)
                gx = 0;
            if (gx >= gridWidth)
                gx = gridWidth - 1;

            int v = int(Z[gy][gx]);
            Vec3b color = getColorFromStops(v, stops, stopColors);

            rowPtr[x * 3 + 0] = color[0]; // B
            rowPtr[x * 3 + 1] = color[1]; // G
            rowPtr[x * 3 + 2] = color[2]; // R
        }
    }

    return heatmap;
}

//-------------------------------------------------------------------
// 6) Dibujar puntos e índices encima de la imagen BGR
//-------------------------------------------------------------------
void visualize_heatmap_points(Mat &img, const vector<pair<double, double>> &coords,
                              Scalar circleColor, Scalar textColor)
{
    for (size_t i = 0; i < coords.size(); i++)
    {
        Point pt((int)coords[i].first, (int)coords[i].second);
        circle(img, pt, 5, circleColor, -1);

        Point textPt = pt + Point(5, -5);
        putText(img, to_string(i), textPt, FONT_HERSHEY_SIMPLEX, 0.4, textColor, 1, LINE_AA);
    }
}

//-------------------------------------------------------------------
// Estructura de parámetros para hilos
//-------------------------------------------------------------------
struct ThreadParams
{
    int startFrame;
    int endFrame;
    // Datos globales para la generación
    const vector<vector<double>> *pressures;    // [frame][sensor]
    const vector<pair<double, double>> *coords; // sensor coords
    vector<Mat> *framesOut;                     // donde guardamos la imagen generada
    // Parámetros para generateHeatMapFrameDownsample
    int widthFinal;
    int heightFinal;
    int gridWidth;
    int gridHeight;
    double radius;
    double smoothness;
    const vector<int> *stops;
    const vector<Vec3b> *stopColors;
};

//-------------------------------------------------------------------
// 7) Función worker para hilos
//    Procesa frames en [startFrame..endFrame)
//-------------------------------------------------------------------
void workerGenerateFrames(const ThreadParams &p)
{
    for (int f = p.startFrame; f < p.endFrame; f++)
    {
        // presiones para frame f
        const vector<double> &press = (*p.pressures)[f];

        // Generar heatmap
        Mat hm = generateHeatMapFrameDownsample(
            *(p.coords), // coords
            press,       // presiones frame f
            p.widthFinal,
            p.heightFinal,
            p.gridWidth,
            p.gridHeight,
            p.radius,
            p.smoothness,
            *(p.stops),
            *(p.stopColors));

        // Guardar en framesOut[f]
        (*p.framesOut)[f] = hm;
    }
}

//-------------------------------------------------------------------
// 8) Función principal para generar animación con hilos
//    - Calculamos frames_left / frames_right en paralelo
//    - Luego combinamos y escribimos a FFmpeg
//-------------------------------------------------------------------
void generate_combined_animation_multithreaded(
    const vector<vector<double>> &pressures_left,
    const vector<vector<double>> &pressures_right,
    const vector<pair<double, double>> &coords_left,
    const vector<pair<double, double>> &coords_right,
    const string &outFilename,
    int widthFinal,  // ancho final (cada plantilla)
    int heightFinal, // alto final
    int gridWidth,   // grid interno
    int gridHeight,
    double radius,
    double smoothness,
    double fps,
    int numThreads) // número de hilos
{
    // Verificar que tengamos frames
    size_t totalFrames = min(pressures_left.size(), pressures_right.size());
    if (totalFrames == 0)
    {
        cerr << "No hay frames para procesar." << endl;
        return;
    }

    // Color stops (mismo approach que en tu React)
    vector<int> stops = {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4095};
    vector<Vec3b> stopColors = {
        Vec3b(255, 0, 0),   // 0:   #0000FF en BGR
        Vec3b(255, 0, 0),   // 500: #0000FF
        Vec3b(255, 255, 0), // 1000: #00FFFF
        Vec3b(0, 255, 0),   // 1500: #00FF00
        Vec3b(0, 255, 255), // 2000: #FFFF00
        Vec3b(0, 165, 255), // 2500: #FFA500
        Vec3b(0, 69, 255),  // 3000: #FF4500
        Vec3b(0, 0, 255),   // 3500: #FF0000
        Vec3b(0, 0, 139),   // 4000: #8B0000
        Vec3b(0, 0, 139)    // 4095: #8B0000
    };

    // 1) Reservar vectores para guardar frames generados
    vector<Mat> frames_left(totalFrames), frames_right(totalFrames);

    // 2) Crear hilos para la izquierda y la derecha
    //    Dividimos frames en lotes para cada hilo
    auto launchThreads = [&](const vector<vector<double>> &pressures,
                             const vector<pair<double, double>> &coords,
                             vector<Mat> &outFrames)
    {
        vector<thread> workers;
        // Dividir totalFrames en numThreads lotes
        int chunkSize = (int(totalFrames) + numThreads - 1) / numThreads;
        int frameStart = 0;
        for (int t = 0; t < numThreads; t++)
        {
            int frameEnd = min(frameStart + chunkSize, (int)totalFrames);

            ThreadParams tp;
            tp.startFrame = frameStart;
            tp.endFrame = frameEnd;
            tp.pressures = &pressures;
            tp.coords = &coords;
            tp.framesOut = &outFrames;
            tp.widthFinal = widthFinal;
            tp.heightFinal = heightFinal;
            tp.gridWidth = gridWidth;
            tp.gridHeight = gridHeight;
            tp.radius = radius;
            tp.smoothness = smoothness;
            tp.stops = &stops;
            tp.stopColors = &stopColors;

            workers.emplace_back(workerGenerateFrames, tp);

            frameStart = frameEnd;
            if (frameStart >= (int)totalFrames)
                break;
        }

        // Esperar que terminen
        for (auto &th : workers)
            th.join();
    };

    // 3) Procesar la izquierda y la derecha en HILOS (cada lado por separado).
    //    Puedes llamar a launchThreads dos veces (una para left, otra para right).
    //    O si quieres más paralelismo, puedes crearlos en paralelo;
    //    pero aquí es sencillo hacerlo secuencialmente:
    launchThreads(pressures_left, coords_left, frames_left);
    launchThreads(pressures_right, coords_right, frames_right);

    // 4) Ya tenemos todos los frames en memoria. Generar video con FFmpeg
    //    Resolución combinada
    int combinedWidth = widthFinal * 2;
    int combinedHeight = heightFinal;

    // Comando FFmpeg
    string cmd = "ffmpeg -y -f rawvideo -pixel_format bgr24 -video_size " +
                 to_string(combinedWidth) + "x" + to_string(combinedHeight) +
                 " -framerate " + to_string(fps) +
                 " -i pipe:0 -c:v libx264 -preset fast -crf 28 -b:v 500k -pix_fmt yuv420p \"" +
                 outFilename + "\"";

    FILE *ffmpeg_pipe = popen(cmd.c_str(), "w");
    if (!ffmpeg_pipe)
    {
        cerr << "Error al iniciar FFmpeg." << endl;
        return;
    }

    // 5) Combinar cada frame y escribirlo
    for (size_t f = 0; f < totalFrames; f++)
    {
        Mat leftF = frames_left[f];
        Mat rightF = frames_right[f];

        // Dibujar puntos en leftF
        visualize_heatmap_points(leftF, coords_left, Scalar(0, 0, 0), Scalar(255, 255, 255));
        // Dibujar puntos en rightF
        visualize_heatmap_points(rightF, coords_right, Scalar(0, 0, 0), Scalar(255, 255, 255));

        // Unir horizontal
        Mat combined;
        hconcat(leftF, rightF, combined);

        // Escribir a FFmpeg
        size_t bytesToWrite = combined.total() * combined.elemSize();
        size_t written = fwrite(combined.data, 1, bytesToWrite, ffmpeg_pipe);
        if (written != bytesToWrite)
        {
            cerr << "Error al escribir frame " << f << endl;
            break;
        }
    }

    pclose(ffmpeg_pipe);
    cout << "Animación generada: " << outFilename << endl;
}

//-------------------------------------------------------------------
// MAIN de ejemplo
//-------------------------------------------------------------------
int main()
{
    // 1) Ajusta según tu gusto
    // int widthFinal = 350; // Resolución final del heatmap (cada plantilla)
    // int heightFinal = 1040;
    int widthFinal = 175, heightFinal = 520;

    // 2) Grid interno (controla pixelación). Cuanto más pequeño, más pixelado.
    int gridWidth = 20;
    int gridHeight = 69;

    // 3) Parámetros de Gaussianas
    double radius = 70.0;
    double smoothness = 2.0;

    // 4) FPS
    double fps = 50.0;

    // 5) Número de hilos
    //    Usa std::thread::hardware_concurrency() si deseas automático
    int numThreads = 4;

    // 6) Leer coordenadas JSON
    vector<pair<double, double>> coords_left, coords_right;
    read_coordinates("leftPoints.json", coords_left);
    read_coordinates("rightPoints.json", coords_right);

    // 7) Leer CSV de presiones
    vector<vector<double>> pressures_left = read_csv("left.csv");
    vector<vector<double>> pressures_right = read_csv("right.csv");

    // 8) Generar la animación en hilos
    generate_combined_animation_multithreaded(
        pressures_left,
        pressures_right,
        coords_left,
        coords_right,
        "combined_pressure_animation_threaded.mp4",
        widthFinal,
        heightFinal,
        gridWidth,
        gridHeight,
        radius,
        smoothness,
        fps,
        numThreads);

    return 0;
}
