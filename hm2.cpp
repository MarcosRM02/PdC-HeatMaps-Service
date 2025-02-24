#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <cmath>
#include <opencv2/opencv.hpp>
#include <Eigen/Dense>
#include <rapidjson/document.h>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <cstdio>

using namespace std;
using namespace cv;
using namespace rapidjson;

struct DataMap
{
    pair<double, double> coordinates;
    vector<double> values;
};

//////////////////////////////////////////////////////////
// 1) Leer Coordenadas (igual a tu versión actual)
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

//////////////////////////////////////////////////////////
// 2) Leer CSV normal (filas = frames, columnas = sensores)
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

//////////////////////////////////////////////////////////
// 3) Generar un heatmap usando un grid reducido + upsample
//    - gridWidth, gridHeight = resolución interna
//    - widthFinal, heightFinal = resolución final
//    - stops y stopColors = para la interpolación
//    - coords = { (x_i, y_i), ... }
//    - pressures = { p_i, ... } (para un frame)
//////////////////////////////////////////////////////////

// Interpolación lineal de color (BGR) entre dos Vec3b
static inline Vec3b interpolateColor(const Vec3b &c1, const Vec3b &c2, double t)
{
    Vec3b result;
    for (int i = 0; i < 3; i++)
    {
        result[i] = static_cast<uchar>((1.0 - t) * c1[i] + t * c2[i]);
    }
    return result;
}

// Aplica la lógica de colorStops al valor v (clamp 0..4095)
Vec3b getColorFromStops(int v,
                        const vector<int> &stops,
                        const vector<Vec3b> &stopColors)
{
    // Clamp
    if (v < 0)
        v = 0;
    if (v > 4095)
        v = 4095;

    // Buscar en qué rango cae
    for (size_t i = 0; i < stops.size() - 1; i++)
    {
        int s1 = stops[i], s2 = stops[i + 1];
        if (v >= s1 && v <= s2)
        {
            double t = (double)(v - s1) / (double)(s2 - s1);
            return interpolateColor(stopColors[i], stopColors[i + 1], t);
        }
    }
    // Por si algo sale fuera
    return stopColors.back();
}

// Genera el heatmap final en BGR (8UC3)
Mat generateHeatMapFrameDownsample(
    const vector<pair<double, double>> &coords,
    const vector<double> &pressures,
    int widthFinal,
    int heightFinal,
    int gridWidth,
    int gridHeight,
    double radius,
    double smoothness,
    const vector<int> &stops,
    const vector<Vec3b> &stopColors)
{
    // 1) Creamos la matriz Z en resolución gridHeight x gridWidth
    vector<vector<double>> Z(gridHeight, vector<double>(gridWidth, 0.0));

    // 2) Llenamos Z con la suma de Gaussianas
    //    (x, y) en coords[i], presión = pressures[i]
    // Mapeo lineal de índices de grid a coordenadas finales:
    //    Xg(gx) = (gx + 0.5) / gridWidth  * widthFinal
    //    Yg(gy) = (gy + 0.5) / gridHeight * heightFinal
    // (usar +0.5 para centrar en el píxel).
    for (size_t i = 0; i < coords.size(); i++)
    {
        double x0 = coords[i].first;
        double y0 = coords[i].second;
        double p0 = pressures[i];

        for (int gy = 0; gy < gridHeight; gy++)
        {
            double Yg = (gy + 0.5) / (double)gridHeight * heightFinal;
            for (int gx = 0; gx < gridWidth; gx++)
            {
                double Xg = (gx + 0.5) / (double)gridWidth * widthFinal;
                double dx = Xg - x0;
                double dy = Yg - y0;
                double dist2 = dx * dx + dy * dy;
                double contribution = p0 * exp(-smoothness * (dist2 / (radius * radius)));
                Z[gy][gx] += contribution;
            }
        }
    }

    // 3) Clampeamos Z a [0..4095]
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

    // 4) Creamos la imagen final BGR
    Mat heatmap(heightFinal, widthFinal, CV_8UC3, Scalar(0, 0, 0));

    // 5) Upsample: para cada píxel (x,y) en la imagen final,
    //    mapeamos a una celda (gx, gy) del grid y tomamos Z.
    for (int y = 0; y < heightFinal; y++)
    {
        // Calcular gy
        int gy = (int)((double)y / (double)heightFinal * gridHeight);
        if (gy < 0)
            gy = 0;
        if (gy >= gridHeight)
            gy = gridHeight - 1;

        uchar *rowPtr = heatmap.ptr<uchar>(y);

        for (int x = 0; x < widthFinal; x++)
        {
            int gx = (int)((double)x / (double)widthFinal * gridWidth);
            if (gx < 0)
                gx = 0;
            if (gx >= gridWidth)
                gx = gridWidth - 1;

            int value = (int)Z[gy][gx]; // 0..4095
            Vec3b color = getColorFromStops(value, stops, stopColors);

            rowPtr[x * 3 + 0] = color[0]; // B
            rowPtr[x * 3 + 1] = color[1]; // G
            rowPtr[x * 3 + 2] = color[2]; // R
        }
    }

    return heatmap;
}

// Dibuja puntos e índices sobre la imagen BGR
void visualize_heatmap_points(Mat &img, const vector<pair<double, double>> &coords, Scalar circleColor, Scalar textColor)
{
    for (size_t i = 0; i < coords.size(); i++)
    {
        Point pt((int)coords[i].first, (int)coords[i].second);
        circle(img, pt, 5, circleColor, -1);

        // Un poco a la derecha/abajo
        Point textPt = pt + Point(5, -5);
        putText(img, to_string(i), textPt, FONT_HERSHEY_SIMPLEX, 0.4, textColor, 1, LINE_AA);
    }
}

//////////////////////////////////////////////////////////
// 4) Función para generar la animación (50 FPS)
//////////////////////////////////////////////////////////
void generate_combined_animation_sequential(
    const vector<vector<double>> &pressures_left,
    const vector<vector<double>> &pressures_right,
    const vector<pair<double, double>> &coords_left,
    const vector<pair<double, double>> &coords_right,
    const string &outFilename,
    int widthFinal,  // ancho de cada plantilla
    int heightFinal, // alto  de cada plantilla
    double fps)
{
    // Definir parámetros del grid
    int gridWidth = 20;  // como en tu React
    int gridHeight = 69; // idem
    double radius = 70.0;
    double smoothness = 2.0;

    // ColorStops y stops (mismos que en React)
    vector<int> stops = {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4095};
    // Notar que en BGR:
    // #0000FF -> (255, 0,   0)
    // #00FFFF -> (255, 255, 0)
    // ...
    vector<Vec3b> stopColors = {
        Vec3b(255, 0, 0),   // 0: "#0000FF"
        Vec3b(255, 0, 0),   // 500
        Vec3b(255, 255, 0), // 1000: "#00FFFF"
        Vec3b(0, 255, 0),   // 1500: "#00FF00"
        Vec3b(0, 255, 255), // 2000: "#FFFF00"
        Vec3b(0, 165, 255), // 2500: "#FFA500"
        Vec3b(0, 69, 255),  // 3000: "#FF4500"
        Vec3b(0, 0, 255),   // 3500: "#FF0000"
        Vec3b(0, 0, 139),   // 4000: "#8B0000"
        Vec3b(0, 0, 139)    // 4095
    };

    // Verificar que pressures_left.size() == pressures_right.size() (mismo num. frames)
    size_t totalFrames = min(pressures_left.size(), pressures_right.size());
    if (totalFrames == 0)
    {
        cerr << "No hay frames para procesar." << endl;
        return;
    }

    // Dimensiones combinadas (lado a lado)
    int combinedWidth = widthFinal * 2;
    int combinedHeight = heightFinal;

    // Construir comando FFmpeg
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

    // Generar cada frame
    for (size_t f = 0; f < totalFrames; f++)
    {
        // Mapa left
        const vector<double> &pl = pressures_left[f]; // presiones frame f
        Mat leftFrame = generateHeatMapFrameDownsample(
            coords_left, pl,
            widthFinal, heightFinal,
            gridWidth, gridHeight,
            radius, smoothness,
            stops, stopColors);

        // Mapa right
        const vector<double> &pr = pressures_right[f];
        Mat rightFrame = generateHeatMapFrameDownsample(
            coords_right, pr,
            widthFinal, heightFinal,
            gridWidth, gridHeight,
            radius, smoothness,
            stops, stopColors);

        // Dibujar puntos
        // (negro para el círculo, blanco para el texto)
        visualize_heatmap_points(leftFrame, coords_left, Scalar(0, 0, 0), Scalar(255, 255, 255));

        // Para el rightFrame, las coords son relativas; se dibujan directo:
        visualize_heatmap_points(rightFrame, coords_right, Scalar(0, 0, 0), Scalar(255, 255, 255));

        // Combinar horizontalmente
        Mat combined;
        hconcat(leftFrame, rightFrame, combined);

        // Escribir al pipe
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

//////////////////////////////////////////////////////////
// 5) MAIN
//////////////////////////////////////////////////////////
int main()
{
    // 1) Resolución final de cada plantilla (igual que en tu React, 350x1040 aprox)
    // int width = 350;
    // int height = 1040;
    int width = 175, height = 520;
    double fps = 50.0;

    // 2) Leer coordenadas (JSON)
    vector<pair<double, double>> coords_left, coords_right;
    read_coordinates("leftPoints.json", coords_left);
    read_coordinates("rightPoints.json", coords_right);

    // 3) Leer CSV: filas = frames, cols = (n sensores)
    vector<vector<double>> pressures_left = read_csv("left.csv");
    vector<vector<double>> pressures_right = read_csv("right.csv");

    // 4) Generar animación lado a lado
    generate_combined_animation_sequential(
        pressures_left,
        pressures_right,
        coords_left,
        coords_right,
        "combined_pressure_animation_grid_upsample.mp4",
        width,
        height,
        fps);

    return 0;
}
