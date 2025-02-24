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

//------------------------------------------------------------
// 1) Leer coordenadas (JSON)
//------------------------------------------------------------
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

//------------------------------------------------------------
// 2) Leer CSV: filas = frames, columnas = sensores
//------------------------------------------------------------
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

//------------------------------------------------------------
// 3) Función para generar un heatmap (doble) y luego applyColorMap(JET)
//    - Sin stops manuales. Se emula `cmap='jet'` con OpenCV.
//------------------------------------------------------------
cv::Mat generate_heatmap_jet(
    const vector<pair<double, double>> &coords,
    const vector<double> &pressures,
    int widthFinal,
    int heightFinal,
    int gridW,
    int gridH,
    double radius,
    double smoothness)
{
    // A) Construimos Z en double, tamaño (gridH x gridW)
    vector<vector<double>> Z(gridH, vector<double>(gridW, 0.0));

    // Llenar Z con Gaussiana
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

    // B) Clampear Z a [0..4095]
    for (int gy = 0; gy < gridH; gy++)
    {
        for (int gx = 0; gx < gridW; gx++)
        {
            if (Z[gy][gx] < 0.0)
                Z[gy][gx] = 0.0;
            else if (Z[gy][gx] > 4095)
                Z[gy][gx] = 4095.0;
        }
    }

    // C) Crear Mat final (CV_8UC1) de (heightFinal x widthFinal)
    //    => Escalar 0..4095 a 0..255 y upsample
    Mat grayImg(heightFinal, widthFinal, CV_8UC1);

    for (int y = 0; y < heightFinal; y++)
    {
        // Mapeo y->gy
        int gy = int((double)y / heightFinal * gridH);
        if (gy >= gridH)
            gy = gridH - 1;
        uchar *rowPtr = grayImg.ptr<uchar>(y);

        for (int x = 0; x < widthFinal; x++)
        {
            int gx = int((double)x / widthFinal * gridW);
            if (gx >= gridW)
                gx = gridW - 1;

            // Escalar a [0..255]
            double v = Z[gy][gx];
            // v in [0..4095], => scaled in [0..255]
            int scaled = (int)(v * (255.0 / 4095.0));
            if (scaled < 0)
                scaled = 0;
            if (scaled > 255)
                scaled = 255;

            rowPtr[x] = (uchar)scaled;
        }
    }

    // D) Aplicar colormap JET
    //    (Igual que en Python con cmap='jet', vmin=0..vmax=255)
    Mat colorImg;
    applyColorMap(grayImg, colorImg, COLORMAP_JET);

    return colorImg;
}

//------------------------------------------------------------
// 4) Dibujar puntos + índices
//------------------------------------------------------------
void draw_points_indices(Mat &img, const vector<pair<double, double>> &coords)
{
    Scalar circleColor(0, 0, 0);     // negro
    Scalar textColor(255, 255, 255); // blanco
    for (size_t i = 0; i < coords.size(); i++)
    {
        Point pt((int)coords[i].first, (int)coords[i].second);
        circle(img, pt, 5, circleColor, -1);

        // Pequeño offset para el índice
        Point txtPos = pt + Point(5, -5);
        putText(img, to_string(i), txtPos, FONT_HERSHEY_SIMPLEX, 0.4, textColor, 1, LINE_AA);
    }
}

//------------------------------------------------------------
// 5) Estructura de hilos
//------------------------------------------------------------
struct ThreadParams
{
    int startFrame, endFrame;
    const vector<vector<double>> *pressures;
    const vector<pair<double, double>> *coords;
    vector<Mat> *outFrames;
    int wFinal, hFinal;
    int gridW, gridH;
    double radius, smooth;
};

void workerFunction(const ThreadParams &tp)
{
    for (int f = tp.startFrame; f < tp.endFrame; f++)
    {
        const vector<double> &framePress = (*tp.pressures)[f];
        Mat heatmap = generate_heatmap_jet(
            *(tp.coords),
            framePress,
            tp.wFinal, tp.hFinal,
            tp.gridW, tp.gridH,
            tp.radius, tp.smooth);
        (*tp.outFrames)[f] = heatmap;
    }
}

//------------------------------------------------------------
// 6) Crear la barra de colores con applyColorMap(JET).
//    - Generamos un gradiente 0..255 vertical y aplicamos JET
//    - Luego lo rotulamos con los valores equivalentes (0..4095).
//------------------------------------------------------------
Mat create_jet_colorbar(int barWidth, int barHeight)
{
    // A) Generar gradiente vertical 0..255 (arriba=255, abajo=0, para que sea
    //    azul abajo y rojo arriba). Si deseas al revés, invierte el bucle.
    Mat grayGrad(barHeight, barWidth, CV_8UC1);
    for (int y = 0; y < barHeight; y++)
    {
        // 0 en la base => y=0 -> 255, y=barHeight-1 -> 0
        double alpha = double(y) / (barHeight - 1);
        int val = (int)(255.0 * (1.0 - alpha)); // 255 arriba, 0 abajo
        for (int x = 0; x < barWidth; x++)
        {
            grayGrad.at<uchar>(y, x) = (uchar)val;
        }
    }

    // B) applyColorMap(JET)
    Mat colorBar;
    applyColorMap(grayGrad, colorBar, COLORMAP_JET);

    return colorBar;
}

//------------------------------------------------------------
// 6-b) Dibujar labels en la barra (0..4095). Ej: 0, 500, 1000...
//    Ajustamos la posición vertical según val*(barHeight/4095).
//------------------------------------------------------------
void annotate_colorbar(Mat &colorBar,
                       int barWidth,
                       vector<int> ticks) // e.g. {0, 500, 1000,..., 4000}
{
    // Borde o fondo: (opcional) si quieres un margen extra
    // Supongamos que colorBar ya ocupa barWidth de ancho

    int BH = colorBar.rows;
    int BW = colorBar.cols;

    // Dibujamos pequeña línea y texto para cada tick
    for (auto val : ticks)
    {
        // val en [0..4095]
        // invertimos: 0 => bottom, 4095 => top
        double ratio = double(val) / 4095.0;
        int posY = BH - 1 - int(ratio * (BH - 1));

        // Marca horizontal
        line(colorBar,
             Point(barWidth, posY),
             Point(barWidth + 5, posY),
             Scalar(0, 0, 0), 1);

        // Texto
        putText(colorBar,
                to_string(val),
                Point(barWidth + 8, posY + 4),
                FONT_HERSHEY_SIMPLEX,
                0.4,
                Scalar(0, 0, 0),
                1, LINE_AA);
    }

    // Título
    putText(colorBar, "Pressure",
            Point(2, BH - 50), // Ajusta la posición del título aquí
            FONT_HERSHEY_SIMPLEX,
            0.5,
            Scalar(0, 0, 0),
            1, LINE_AA);
}

//------------------------------------------------------------
// 7) Generar animación final con threads + JET + colorbar
//------------------------------------------------------------
void generate_combined_animation_JET(
    const vector<vector<double>> &pressures_left,
    const vector<vector<double>> &pressures_right,
    const vector<pair<double, double>> &coords_left,
    const vector<pair<double, double>> &coords_right,
    const string &outFilename,
    int wFinal, int hFinal,
    int gridW, int gridH,
    double radius, double smooth,
    double fps,
    int numThreads,
    int legendTotalWidth // Ancho total de la barra de color
)
{
    // 1) Calcular dimensiones del frame completo con fondo blanco
    int margin = 50; // Margen extra para separar elementos
    int finalWidth = (wFinal * 2) + legendTotalWidth + (margin * 3);
    int finalHeight = hFinal + (margin * 2);

    // 2) Crear la barra de colores
    int barWidth = 20;
    int barHeight = hFinal - 100; // Reducir la altura de la barra de colores
    Mat colorBar = create_jet_colorbar(barWidth, barHeight);

    // Contenedor con fondo blanco para la barra de colores
    Mat legendContainer(barHeight, legendTotalWidth, CV_8UC3, Scalar(255, 255, 255));
    Rect roiBar(10, 0, barWidth, barHeight);
    colorBar.copyTo(legendContainer(roiBar));

    //  Agregar etiquetas de escala
    vector<int> tickVals = {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000};
    annotate_colorbar(legendContainer, 10 + barWidth, tickVals);

    // 3) Inicializar FFmpeg
    string cmd = "ffmpeg -y -f rawvideo -pixel_format bgr24 -video_size " +
                 to_string(finalWidth) + "x" + to_string(finalHeight) +
                 " -framerate " + to_string(fps) +
                 " -i pipe:0 -c:v libx264 -preset fast -crf 28 -b:v 500k -pix_fmt yuv420p \"" +
                 outFilename + "\"";

    FILE *ffmpeg_pipe = popen(cmd.c_str(), "w");
    if (!ffmpeg_pipe)
    {
        cerr << "Error al iniciar FFmpeg." << endl;
        return;
    }

    // 4) Generar cada frame dentro de un "div" visual en OpenCV
    for (size_t f = 0; f < pressures_left.size(); f++)
    {
        Mat leftF = generate_heatmap_jet(coords_left, pressures_left[f], wFinal, hFinal, gridW, gridH, radius, smooth);
        Mat rightF = generate_heatmap_jet(coords_right, pressures_right[f], wFinal, hFinal, gridW, gridH, radius, smooth);

        draw_points_indices(leftF, coords_left);
        draw_points_indices(rightF, coords_right);

        // Fondo blanco para toda la imagen
        Mat finalFrame(finalHeight, finalWidth, CV_8UC3, Scalar(255, 255, 255));

        // Posiciones dentro del frame (tipo "divs")
        int leftX = margin;
        int rightX = leftX + wFinal + margin;
        int legendX = rightX + wFinal + margin;
        int topY = margin;

        // Copiar cada parte en su posición
        leftF.copyTo(finalFrame(Rect(leftX, topY, wFinal, hFinal)));
        rightF.copyTo(finalFrame(Rect(rightX, topY, wFinal, hFinal)));
        legendContainer.copyTo(finalFrame(Rect(legendX, topY + 35, legendTotalWidth, barHeight)));

        // Escribir a FFmpeg
        size_t bytesToWrite = finalFrame.total() * finalFrame.elemSize();
        fwrite(finalFrame.data, 1, bytesToWrite, ffmpeg_pipe);
    }

    // Cerrar FFmpeg
    pclose(ffmpeg_pipe);
    cout << "Animación generada con JET: " << outFilename << endl;
}

//------------------------------------------------------------
// 8) MAIN de ejemplo
//------------------------------------------------------------
int main()
{
    // Ajusta según tu caso
    int wFinal = 175;
    int hFinal = 520;
    int gridW = 20;
    int gridH = 69;
    double radius = 70.0;
    double smoothness = 2.0;
    double fps = 50.0;
    int numThreads = 4;

    // Ancho total de la leyenda (p.ej. 80 px para la banda + texto)
    int legendWidth = 80;

    // 1) Cargar coords (JSON)
    vector<pair<double, double>> coords_left, coords_right;
    read_coordinates("leftPoints.json", coords_left);
    read_coordinates("rightPoints.json", coords_right);

    // 2) Cargar CSV
    vector<vector<double>> pressures_left = read_csv("left.csv");
    vector<vector<double>> pressures_right = read_csv("right.csv");

    // 3) Generar animación
    generate_combined_animation_JET(
        pressures_left,
        pressures_right,
        coords_left,
        coords_right,
        "combined_pressure_animation_jet.mp4",
        wFinal, hFinal,
        gridW, gridH,
        radius, smoothness,
        fps,
        numThreads,
        legendWidth);

    return 0;
}
