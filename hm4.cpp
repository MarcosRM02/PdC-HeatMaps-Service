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

// RapidJSON
#include <rapidjson/document.h>

using namespace std;
using namespace cv;
using namespace rapidjson;

////////////////////////////////////////////////////////////////////////
// Lectura de JSON y CSV
////////////////////////////////////////////////////////////////////////

void read_coordinates(const string &filename, vector<pair<double, double>> &coordinates)
{
    ifstream file(filename);
    if (!file.is_open())
    {
        cerr << "No se pudo abrir " << filename << endl;
        return;
    }
    stringstream buffer;
    buffer << file.rdbuf();
    string json_str = buffer.str();

    Document doc;
    doc.Parse(json_str.c_str());
    if (doc.HasParseError() || !doc.IsArray())
    {
        cerr << "Error parseando " << filename << " o no es un array." << endl;
        return;
    }

    for (SizeType i = 0; i < doc.Size(); i++)
    {
        if (doc[i].HasMember("x") && doc[i].HasMember("y") &&
            doc[i]["x"].IsDouble() && doc[i]["y"].IsDouble())
        {
            double x = doc[i]["x"].GetDouble();
            double y = doc[i]["y"].GetDouble();
            coordinates.emplace_back(x, y);
        }
    }
}

vector<vector<double>> read_csv(const string &filename)
{
    vector<vector<double>> data;
    ifstream file(filename);
    if (!file.is_open())
    {
        cerr << "No se pudo abrir " << filename << endl;
        return data;
    }

    string line;
    while (getline(file, line))
    {
        vector<double> row;
        stringstream ss(line);
        string val;
        while (getline(ss, val, ','))
        {
            try
            {
                row.push_back(stod(val));
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

////////////////////////////////////////////////////////////////////////
// Interpolación de color + stops
////////////////////////////////////////////////////////////////////////

static inline Vec3b interpolateColor(const Vec3b &c1, const Vec3b &c2, double t)
{
    Vec3b out;
    for (int i = 0; i < 3; i++)
        out[i] = (uchar)((1.0 - t) * c1[i] + t * c2[i]);
    return out;
}

Vec3b getColorFromStops(int v,
                        const vector<int> &stops,
                        const vector<Vec3b> &colors)
{
    if (v < 0)
        v = 0;
    if (v > 4095)
        v = 4095;

    for (size_t i = 0; i < stops.size() - 1; i++)
    {
        if (v >= stops[i] && v <= stops[i + 1])
        {
            double t = double(v - stops[i]) / double(stops[i + 1] - stops[i]);
            return interpolateColor(colors[i], colors[i + 1], t);
        }
    }
    return colors.back(); // fallback
}

////////////////////////////////////////////////////////////////////////
// Generar HeatMap con grid reducido + upsample (sin alterar coords)
////////////////////////////////////////////////////////////////////////

Mat generateHeatMapFrame(
    const vector<pair<double, double>> &coords,
    const vector<double> &pressures,
    int widthFinal,
    int heightFinal,
    int gridW,
    int gridH,
    double radius,
    double smoothness,
    const vector<int> &stops,
    const vector<Vec3b> &stopColors)
{
    // Grid Z
    vector<vector<double>> Z(gridH, vector<double>(gridW, 0.0));

    // Rellenar el grid con gaussianas
    for (size_t i = 0; i < coords.size(); i++)
    {
        double x0 = coords[i].first;
        double y0 = coords[i].second;
        double p0 = pressures[i];

        for (int gy = 0; gy < gridH; gy++)
        {
            double Yg = (gy + 0.5) / double(gridH) * heightFinal;
            for (int gx = 0; gx < gridW; gx++)
            {
                double Xg = (gx + 0.5) / double(gridW) * widthFinal;
                double dx = Xg - x0;
                double dy = Yg - y0;
                double dist2 = dx * dx + dy * dy;
                double val = p0 * exp(-smoothness * (dist2 / (radius * radius)));
                Z[gy][gx] += val;
            }
        }
    }

    // Clamp [0..4095]
    for (int gy = 0; gy < gridH; gy++)
    {
        for (int gx = 0; gx < gridW; gx++)
        {
            if (Z[gy][gx] < 0.0)
                Z[gy][gx] = 0.0;
            if (Z[gy][gx] > 4095.0)
                Z[gy][gx] = 4095.0;
        }
    }

    Mat heatmap(heightFinal, widthFinal, CV_8UC3, Scalar(0, 0, 0));

    // Upsample
    for (int y = 0; y < heightFinal; y++)
    {
        int gy = int((double)y / heightFinal * gridH);
        if (gy >= gridH)
            gy = gridH - 1;
        uchar *rowPtr = heatmap.ptr<uchar>(y);
        for (int x = 0; x < widthFinal; x++)
        {
            int gx = int((double)x / widthFinal * gridW);
            if (gx >= gridW)
                gx = gridW - 1;

            int v = (int)Z[gy][gx];
            Vec3b c = getColorFromStops(v, stops, stopColors);

            rowPtr[x * 3 + 0] = c[0];
            rowPtr[x * 3 + 1] = c[1];
            rowPtr[x * 3 + 2] = c[2];
        }
    }

    return heatmap;
}

////////////////////////////////////////////////////////////////////////
// Dibujar puntos e índices
////////////////////////////////////////////////////////////////////////
void drawPoints(Mat &img, const vector<pair<double, double>> &coords)
{
    Scalar circleColor(0, 0, 0);     // negro
    Scalar textColor(255, 255, 255); // blanco
    for (size_t i = 0; i < coords.size(); i++)
    {
        Point pt((int)coords[i].first, (int)coords[i].second);
        circle(img, pt, 5, circleColor, -1);

        Point txtPos = pt + Point(5, -5);
        putText(img, to_string(i), txtPos, FONT_HERSHEY_SIMPLEX, 0.4, textColor, 1, LINE_AA);
    }
}

////////////////////////////////////////////////////////////////////////
// Worker e hilos
////////////////////////////////////////////////////////////////////////
struct ThreadParams
{
    int startFrame, endFrame;
    const vector<vector<double>> *pressures;
    const vector<pair<double, double>> *coords;
    vector<Mat> *outFrames;
    // Param heatmap
    int wFinal, hFinal;
    int gridW, gridH;
    double radius, smooth;
    const vector<int> *stops;
    const vector<Vec3b> *stopColors;
};

void workerFunction(const ThreadParams &p)
{
    for (int f = p.startFrame; f < p.endFrame; f++)
    {
        const vector<double> &press = (*p.pressures)[f];
        Mat hm = generateHeatMapFrame(
            *(p.coords), press,
            p.wFinal, p.hFinal,
            p.gridW, p.gridH,
            p.radius, p.smooth,
            *(p.stops), *(p.stopColors));
        (*p.outFrames)[f] = hm;
    }
}

////////////////////////////////////////////////////////////////////////
// Crear una barra de color "decente" y parecida al screenshot
////////////////////////////////////////////////////////////////////////
Mat createColorBar(
    int legendWidth,  // ancho total
    int legendHeight, // alto (mismo que frames)
    const vector<int> &stops,
    const vector<Vec3b> &stopColors)
{
    // Fondo gris claro
    Mat legend(legendHeight, legendWidth, CV_8UC3, Scalar(240, 240, 240));

    // Definimos la zona donde irá la "barra" pura de color
    // Dejamos unos márgenes (topMargin, bottomMargin, leftMargin, etc.)
    int topMargin = 20;
    int bottomMargin = 20;
    int leftMargin = 10;
    int barWidth = 20; // anchura de la banda vertical
    // Ajusta si quieres
    int barX = leftMargin;
    int barY = topMargin;
    int barH = legendHeight - (topMargin + bottomMargin);

    // Dibujo de la barra en sí (un rect. de barWidth x barH)
    // 0 = azul abajo, 4095 = rojo arriba
    for (int dy = 0; dy < barH; dy++)
    {
        // dy=0 -> top, dy=barH-1 -> bottom
        // Queremos 0 en la base, 4095 en la cima => invertimos
        double alpha = double(dy) / double(barH - 1);
        double inv = 1.0 - alpha; // 0 bottom, 4095 top
        int val = (int)(inv * 4095.0);

        Vec3b c = getColorFromStops(val, stops, stopColors);

        for (int dx = 0; dx < barWidth; dx++)
        {
            int px = barX + dx;
            int py = barY + dy;
            legend.at<Vec3b>(py, px) = c;
        }
    }

    // Borde negro alrededor de la barra
    rectangle(legend, Point(barX, barY),
              Point(barX + barWidth - 1, barY + barH - 1),
              Scalar(0, 0, 0), 1);

    // Dibujar stops: 0, 500, 1000, 1500, etc.
    for (size_t i = 0; i < stops.size(); i++)
    {
        int st = stops[i]; // 0..4095
        double ratio = double(st) / 4095.0;
        int tickY = barY + barH - 1 - int(ratio * (barH - 1));
        // Pequeña marca horizontal a la derecha de la barra
        line(legend, Point(barX + barWidth, tickY),
             Point(barX + barWidth + 5, tickY),
             Scalar(0, 0, 0), 1);

        // Texto con el valor
        putText(legend, to_string(st),
                Point(barX + barWidth + 8, tickY + 3),
                FONT_HERSHEY_SIMPLEX,
                0.45, // escala
                Scalar(0, 0, 0),
                1, LINE_AA);
    }

    // Título "Pressure" en vertical u horizontal
    // Aquí lo pondremos horizontal cerca de la parte superior
    putText(legend, "Pressure",
            Point(barX, topMargin - 5), // arriba de la barra
            FONT_HERSHEY_SIMPLEX,
            0.5,
            Scalar(0, 0, 0),
            1, LINE_AA);

    return legend;
}

////////////////////////////////////////////////////////////////////////
// Función principal para generar animación
////////////////////////////////////////////////////////////////////////
void generateAnimation(
    const vector<vector<double>> &pressLeft,
    const vector<vector<double>> &pressRight,
    const vector<pair<double, double>> &coordsLeft,
    const vector<pair<double, double>> &coordsRight,
    const string &outFile,
    int wFinal, int hFinal,
    int gridW, int gridH,
    double radius, double smoothness,
    double fps,
    int numThreads,
    int legendWidth)
{
    size_t totalFrames = min(pressLeft.size(), pressRight.size());
    if (totalFrames == 0)
    {
        cerr << "No frames to process." << endl;
        return;
    }

    // Mismos stops y colores
    vector<int> stops = {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000};
    vector<Vec3b> stopColors = {
        Vec3b(255, 0, 0),   // 0: #0000FF en BGR
        Vec3b(255, 0, 0),   // 500
        Vec3b(255, 255, 0), // 1000
        Vec3b(0, 255, 0),   // 1500
        Vec3b(0, 255, 255), // 2000
        Vec3b(0, 165, 255), // 2500
        Vec3b(0, 69, 255),  // 3000
        Vec3b(0, 0, 255),   // 3500
        Vec3b(0, 0, 139),   // 4000
    };

    // Preparamos vectores de frames
    vector<Mat> framesL(totalFrames), framesR(totalFrames);

    // Lambda para lanzar hilos
    auto launchThreads = [&](const vector<vector<double>> &pressures,
                             const vector<pair<double, double>> &coords,
                             vector<Mat> &outFrames)
    {
        vector<thread> pool;
        int chunk = (int(totalFrames) + numThreads - 1) / numThreads;
        int start = 0;
        for (int t = 0; t < numThreads; t++)
        {
            int end = min(start + chunk, (int)totalFrames);
            ThreadParams tp;
            tp.startFrame = start;
            tp.endFrame = end;
            tp.pressures = &pressures;
            tp.coords = &coords;
            tp.outFrames = &outFrames;
            tp.wFinal = wFinal;
            tp.hFinal = hFinal;
            tp.gridW = gridW;
            tp.gridH = gridH;
            tp.radius = radius;
            tp.smooth = smoothness;
            tp.stops = &stops;
            tp.stopColors = &stopColors;
            pool.emplace_back(workerFunction, tp);

            start = end;
            if (start >= (int)totalFrames)
                break;
        }
        for (auto &th : pool)
            th.join();
    };

    // Generar frames en paralelo
    launchThreads(pressLeft, coordsLeft, framesL);
    launchThreads(pressRight, coordsRight, framesR);

    // Crear la barra (leyenda) => height = hFinal
    Mat colorBar = createColorBar(legendWidth, hFinal, stops, stopColors);

    // El ancho final = (wFinal*2) + legendWidth
    int finalW = (wFinal * 2) + legendWidth;
    int finalH = hFinal;

    // Comando ffmpeg
    string cmd = "ffmpeg -y -f rawvideo -pixel_format bgr24 -video_size " +
                 to_string(finalW) + "x" + to_string(finalH) +
                 " -framerate " + to_string(fps) +
                 " -i pipe:0 -c:v libx264 -preset fast -crf 28 -b:v 500k -pix_fmt yuv420p \"" +
                 outFile + "\"";

    FILE *pipe = popen(cmd.c_str(), "w");
    if (!pipe)
    {
        cerr << "Error al iniciar FFmpeg." << endl;
        return;
    }

    // Combinar y escribir
    for (size_t f = 0; f < totalFrames; f++)
    {
        Mat leftF = framesL[f];
        Mat rightF = framesR[f];

        // Dibujar puntos
        drawPoints(leftF, coordsLeft);
        drawPoints(rightF, coordsRight);

        // hconcat L+R
        Mat combinedLR;
        hconcat(leftF, rightF, combinedLR);

        // hconcat combinedLR + colorBar
        Mat finalFrame;
        hconcat(combinedLR, colorBar, finalFrame);

        // Escribir
        size_t nbytes = finalFrame.total() * finalFrame.elemSize();
        if (fwrite(finalFrame.data, 1, nbytes, pipe) != nbytes)
        {
            cerr << "Error escribiendo frame " << f << endl;
            break;
        }
    }
    pclose(pipe);
    cout << "Animacion generada: " << outFile << endl;
}

////////////////////////////////////////////////////////////////////////
// MAIN de ejemplo
////////////////////////////////////////////////////////////////////////
int main()
{
    // Ajusta a tus valores
    int wFinal = 175;
    int hFinal = 520;
    int gridWidth = 20;
    int gridHeight = 69;
    double radius = 70.0;
    double smoothness = 2.0;
    double fps = 50.0;
    int numThreads = 4;

    // Barra de colores (ancho 60 px, ajusta si quieres)
    int legendWidth = 100;

    // 1) Leer coords
    vector<pair<double, double>> coords_left, coords_right;
    read_coordinates("leftPoints.json", coords_left);
    read_coordinates("rightPoints.json", coords_right);

    // 2) Leer CSV
    vector<vector<double>> pressures_left = read_csv("left.csv");
    vector<vector<double>> pressures_right = read_csv("right.csv");

    // 3) Generar animacion
    generateAnimation(
        pressures_left,
        pressures_right,
        coords_left,
        coords_right,
        "combined_pressure_animation_threaded_better_bar.mp4",
        wFinal, hFinal,
        gridWidth, gridHeight,
        radius, smoothness,
        fps,
        numThreads,
        legendWidth);

    return 0;
}
