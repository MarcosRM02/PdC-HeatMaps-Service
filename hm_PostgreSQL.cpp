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

#include <hiredis/hiredis.h> // Incluir hiredis para Redis

#include <curl/curl.h>

#include <libpq-fe.h> // Incluir libpq-fe para PostgreSQL

#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0777) // Comando mkdir en Linux/macOS

using namespace std;
using namespace cv;
using namespace rapidjson;

// constexpr asegura que se evalúe en tiempo de compilación, lo cual puede ayudar al compilador a optimizar el código
constexpr int wFinal = 175;
constexpr int hFinal = 520;
constexpr int gridW = 20;
constexpr int gridH = 69;
constexpr double radius = 70.0;
constexpr double smoothness = 2.0;
constexpr double fps = 32.0;
constexpr int numThreads = 4;
constexpr int legendWidth = 80;
const std::string baseUrl = "http://ssith-backend-container:3000/swData/generateCSV/"; // http://ssith-backend-container:3000 -- localhost:3000
const std::string redisQueue = "redis_queue";

PGconn *cnn = NULL;
PGresult *result = NULL;

const char *host = "sqlDB";
const char *port = "5432";
const char *dataBase = "ssith-db";
const char *user = "admin";
const char *passwd = "admin";

const std::string apiUser = "marcos"; // Más adelante miraremos una alternativa a esto.
const std::string apiPassword = "zodv38jN0Bty5ns1";
const std::string loginUrl = "http://ssith-backend-container:3000/authentication/serviceLogin/"; // http://ssith-backend-container:3000

//------------------------------------------------------------
// 20) Login del servicio, para poder obtener su token jwt
//------------------------------------------------------------
// Callback para almacenar la respuesta del servidor
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *output)
{
    size_t total_size = size * nmemb;
    output->append((char *)contents, total_size);
    return total_size;
}

std::string loginToAPI()
{
    CURL *curl;
    CURLcode res;
    string response;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if (curl)
    {

        string jsonData = "{\"user\":\"" + apiUser + "\",\"password\":\"" + apiPassword + "\"}";

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, loginUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK)
        {
            cerr << "Error en la solicitud: " << curl_easy_strerror(res) << endl;
        }
        else
        {
            cout << "Respuesta del servidor: " << response << endl;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();

    return response; // Devuelve el token jwt
}

std::string &getToken()
{

    // TO-DO: Implementar un mecanismo de renovación del token
    static string token;
    if (token.empty()) // Solo hace login si el token aún no está definido
    {
        token = loginToAPI();
    }
    return token;
}

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
    const vector<int> &pressures,
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
    const vector<vector<int>> *pressures;
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
        const vector<int> &framePress = (*tp.pressures)[f];
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

        //  Marca horizontal
        line(colorBar,
             Point(barWidth, posY),
             Point(barWidth + 5, posY),
             Scalar(0, 0, 0), 1);

        if (val == 0)
        {
            // Texto
            putText(colorBar,
                    to_string(val),
                    Point(barWidth + 8, posY),
                    FONT_HERSHEY_SIMPLEX,
                    0.4,
                    Scalar(0, 0, 0),
                    1, LINE_AA);
        }
        else
        {
            // Texto
            putText(colorBar,
                    to_string(val),
                    Point(barWidth + 8, posY + 4),
                    FONT_HERSHEY_SIMPLEX,
                    0.4,
                    Scalar(0, 0, 0),
                    1, LINE_AA);
        }
    }
}

//------------------------------------------------------------
// 7) Generar animación final con threads + JET + colorbar
//------------------------------------------------------------
void generate_combined_animation_JET(
    const vector<vector<int>> &pressures_left,
    const vector<vector<int>> &pressures_right,
    const vector<pair<double, double>> &coords_left,
    const vector<pair<double, double>> &coords_right,
    const string &outFilename)

{
    // 1) Calcular dimensiones del frame completo con fondo blanco
    int margin = 50; // Margen extra para separar elementos
    int finalWidth = (wFinal * 2) + legendWidth + (margin * 3);
    int finalHeight = hFinal + (margin * 2);

    // 2) Crear la barra de colores
    int barWidth = 20;
    int barHeight = hFinal - 20; // Reducir la altura de la barra de colores
    Mat colorBar = create_jet_colorbar(barWidth, barHeight);

    // Contenedor con fondo blanco para la barra de colores
    Mat legendContainer(barHeight, legendWidth, CV_8UC3, Scalar(255, 255, 255));
    Rect roiBar(10, 0, barWidth, barHeight);
    colorBar.copyTo(legendContainer(roiBar));

    //  Agregar etiquetas de escala
    vector<int> tickVals = {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000}; //  vector<int> tickVals = {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000};
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
        Mat leftF = generate_heatmap_jet(coords_left, pressures_left[f], wFinal, hFinal, gridW, gridH, radius, smoothness);
        Mat rightF = generate_heatmap_jet(coords_right, pressures_right[f], wFinal, hFinal, gridW, gridH, radius, smoothness);

        draw_points_indices(leftF, coords_left);
        draw_points_indices(rightF, coords_right);

        // Fondo blanco para toda la imagen
        Mat finalFrame(finalHeight, finalWidth, CV_8UC3, Scalar(255, 255, 255));

        // Título
        putText(finalFrame, "Pressure",
                Point(480, 60), // Ajusta la posición del título aquí
                FONT_HERSHEY_SIMPLEX,
                0.5,
                Scalar(0, 0, 0),
                1, LINE_AA);

        // Posiciones dentro del frame (tipo "divs")
        int leftX = margin;
        int rightX = leftX + wFinal + margin;
        int legendX = rightX + wFinal + margin; // Ajuste para centrar la barra de colores - 35
        int topY = margin;

        // Copiar cada parte en su posición
        leftF.copyTo(finalFrame(Rect(leftX, topY, wFinal, hFinal)));
        rightF.copyTo(finalFrame(Rect(rightX, topY, wFinal, hFinal)));
        legendContainer.copyTo(finalFrame(Rect(legendX - 22, topY + 19, legendWidth, barHeight)));

        // Escribir a FFmpeg
        size_t bytesToWrite = finalFrame.total() * finalFrame.elemSize();
        fwrite(finalFrame.data, 1, bytesToWrite, ffmpeg_pipe);
    }

    // Cerrar FFmpeg
    pclose(ffmpeg_pipe);
    cout << "Animación generada con JET: " << outFilename << endl;
}

//------------------------------------------------------------
// 9) Conexion a la BD Redis para obtener los id de las insoles
//------------------------------------------------------------

// redisContext *connectToRedis(const std::string &host, int port)
// {
//     redisContext *context = redisConnect(host.c_str(), port);
//     if (context == nullptr || context->err)
//     {
//         std::cerr << "❌ Error al conectar con Redis: " << (context ? context->errstr : "Desconocido") << std::endl;
//         return nullptr;
//     }
//     return context;
// }

redisContext *connectToRedis(const std::string &host, int port, const std::string &password)
{
    redisContext *context = redisConnect(host.c_str(), port);
    if (context == nullptr || context->err)
    {
        std::cerr << "❌ Error al conectar con Redis: "
                  << (context ? context->errstr : "Desconocido") << std::endl;
        return nullptr;
    }

    // Si se especificó una contraseña, se envía el comando AUTH
    if (!password.empty())
    {
        redisReply *reply = (redisReply *)redisCommand(context, "AUTH %s", password.c_str());
        if (reply == nullptr)
        {
            std::cerr << "❌ Error al enviar AUTH a Redis." << std::endl;
            redisFree(context);
            return nullptr;
        }
        if (reply->type == REDIS_REPLY_ERROR)
        {
            std::cerr << "❌ Error en la autenticación: " << reply->str << std::endl;
            freeReplyObject(reply);
            redisFree(context);
            return nullptr;
        }
        freeReplyObject(reply);
    }

    return context;
}

//------------------------------------------------------------
// 10) Leer datos de la cola de mensajes por medio de un mecanismo de espera pasiva
//------------------------------------------------------------

redisReply *readFromStream(redisContext *context, const std::string &lastID)
{
    return (redisReply *)redisCommand(context, "XREAD BLOCK 0 STREAMS %s %s", redisQueue.c_str(), lastID.c_str());
}

//------------------------------------------------------------
// 11) Procesar el mensaje recibido de la cola
//------------------------------------------------------------

void processMessage(redisReply *msgData, string &wearableId_L, string &wearableId_R, string &experimentId, string &participantId, string &sWId, string &trialId)
{
    for (size_t k = 0; k < msgData->elements; k += 2)
    {
        redisReply *field = msgData->element[k];
        redisReply *value = msgData->element[k + 1];

        if (field->type == REDIS_REPLY_STRING && value->type == REDIS_REPLY_STRING)
        {
            if (std::string(field->str) == "wearableId_L")
                wearableId_L = std::string(value->str);
            else if (std::string(field->str) == "wearableId_R")
                wearableId_R = std::string(value->str);
            else if (std::string(field->str) == "experimentId")
                experimentId = std::string(value->str);
            else if (std::string(field->str) == "participantId")
                participantId = std::string(value->str);
            else if (std::string(field->str) == "sWId")
                sWId = std::string(value->str);
            else if (std::string(field->str) == "trialId")
                trialId = std::string(value->str);
        }
    }
}

//------------------------------------------------------------
// 13) Creacion del consumidor de la cola redis
//------------------------------------------------------------

void readCoordinates(vector<pair<double, double>> &coords_left, vector<pair<double, double>> &coords_right)
{
    // 1) Cargar coords (JSON)

    read_coordinates("leftPoints.json", coords_left);
    read_coordinates("rightPoints.json", coords_right);
}

//------------------------------------------------------------
// 14) Generar solicitud http a la API para obtener los csv
//------------------------------------------------------------

// Función para convertir un vector de IDs en la estructura correcta de Query Params
std::string buildWearableIdsQuery(const std::string &wearableIdL, const std::string &wearableIdR)
{
    std::ostringstream oss;
    oss << "wearableIds=" << wearableIdL << "&wearableIds=" << wearableIdR;
    return oss.str();
}
// Realizar la petición HTTP y obtener la respuesta en un string.
std::string fetchUrlContent(const std::string &url)
{
    CURL *curl = curl_easy_init();
    std::string readBuffer;
    if (curl)
    {
        struct curl_slist *headers = NULL;
        // Construir la cabecera con el token Bearer
        // std::cout << "Token generado: " << getToken() << std::endl;

        string authHeader = "Authorization: Bearer " + getToken();
        headers = curl_slist_append(headers, authHeader.c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); // Se añaden los headers
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            std::cerr << "❌ Error en la petición HTTP: " << curl_easy_strerror(res) << std::endl;
        }
        curl_slist_free_all(headers); // Liberar memoria de los headers
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

// Extraer el contenido interno removiendo los corchetes externos.
// Se espera que rawResponse tenga el formato: [["fila1 csv1", "fila2 csv1", ...], ["fila1 csv2", "fila2 csv2", ...]]
std::string extractContent(const std::string &rawResponse)
{
    if (rawResponse.size() < 4 ||
        rawResponse.substr(0, 2) != "[[" ||
        rawResponse.substr(rawResponse.size() - 2) != "]]")
    {
        std::cerr << "❌ Formato de respuesta inesperado." << std::endl;
        return "";
    }
    return rawResponse.substr(2, rawResponse.size() - 4);
}

// Separar el contenido en dos partes (izquierda y derecha).
std::pair<std::string, std::string> splitCSVContent(const std::string &content)
{
    size_t delimiterPos = content.find("],");
    if (delimiterPos == std::string::npos)
    {
        std::cerr << "❌ No se encontró el delimitador entre CSVs." << std::endl;
        return {"", ""};
    }

    // A veces el delimitador podría tener espacios; buscamos el patrón "],["
    size_t secondBracketPos = content.find("],[");
    std::string firstPart, secondPart;
    if (secondBracketPos != std::string::npos)
    {
        firstPart = content.substr(0, secondBracketPos);
        secondPart = content.substr(secondBracketPos + 3); // +3 para saltar "],["
    }
    else
    {
        firstPart = content.substr(0, delimiterPos);
        secondPart = content.substr(delimiterPos + 2);
    }
    return {firstPart, secondPart};
}

// Función auxiliar para procesar cada parte del arreglo (string separado por comas y entre comillas)
// Se encarga de extraer cada elemento (fila) y unirlos con "\n".
std::string processPart(const std::string &part)
{
    std::string result;
    size_t pos = 0;
    bool firstRow = true;
    while (pos < part.size())
    {
        // Buscar la comilla de inicio
        size_t startQuote = part.find('"', pos);
        if (startQuote == std::string::npos)
            break;
        // Buscar la comilla de cierre
        size_t endQuote = part.find('"', startQuote + 1);
        if (endQuote == std::string::npos)
            break;
        // Extraer el contenido entre comillas
        std::string row = part.substr(startQuote + 1, endQuote - startQuote - 1);
        if (!firstRow)
            result += "\n";
        result += row;
        firstRow = false;
        pos = endQuote + 1;
        // Saltar comas o espacios que puedan haber
        while (pos < part.size() && (part[pos] == ',' || std::isspace(part[pos])))
            pos++;
    }
    return result;
}

//------------------------------------------------------------
// 15) Leer CSV: filas = frames, columnas = sensores
//------------------------------------------------------------

// Función para convertir un CSV en vector<vector<double>>
std::vector<std::vector<int>> parseCSV(const std::string &csvData)
{
    std::vector<std::vector<int>> data;
    std::stringstream ss(csvData);
    std::string line;
    while (std::getline(ss, line))
    {
        std::vector<int> row;
        std::stringstream lineStream(line);
        std::string value;
        while (std::getline(lineStream, value, ','))
        {
            try
            {
                row.push_back(std::stoi(value));
            }
            catch (...)
            {
                row.push_back(0); // Valor por defecto
            }
        }
        data.push_back(row);
    }
    return data;
}

void fetchCSV(
    const std::string &baseUrl, const std::string &experimentId,
    const std::string &participantId, const std::string &swId,
    const std::string &trialId, const std::string &wearableIdL, const std::string &wearableIdR, std::vector<std::vector<int>> &pressures_left, std::vector<std::vector<int>> &pressures_right)
{

    // Construir la URL
    std::string wearableIdsQuery = buildWearableIdsQuery(wearableIdL, wearableIdR);
    std::string url = baseUrl + experimentId + "/" +
                      participantId + "/" + swId + "/" + trialId + "?" + wearableIdsQuery;

    // Obtener la respuesta HTTP.
    std::string rawResponse = fetchUrlContent(url);

    //  Extraer el contenido interno (sin los corchetes externos).
    std::string content = extractContent(rawResponse);

    // Dividir el contenido en las dos partes de CSV.
    auto parts = splitCSVContent(content);

    // Procesar cada parte para unir las filas en un único string (separadas por saltos de línea)
    std::string leftCsv = processPart(parts.first);
    std::string rightCsv = processPart(parts.second);

    // Convertir cada CSV a vector<vector<int>>
    pressures_left = parseCSV(leftCsv);
    pressures_right = parseCSV(rightCsv);
}

//------------------------------------------------------------
// 12) Conexión con PostgreSQL e insercion de la url de la animación generada
//------------------------------------------------------------

void createConnection()
{
    // Conectarse a PostgreSQL
    cnn = PQsetdbLogin(host, port, NULL, NULL, dataBase, user, passwd);

    if (cnn == NULL || PQstatus(cnn) == CONNECTION_BAD)
    {
        cerr << "Error de conexión a PostgreSQL: " << (cnn ? PQerrorMessage(cnn) : "No se pudo establecer la conexión") << endl;
        PQfinish(cnn);
        return; // Sale de la funcion si hay error
    }

    cout << "Conectado a PostgreSQL!" << endl;
}

void updateTrial(const string &url, const string &trialId)
{
    createConnection();
    // Consulta SQL con parámetros ($1 y $2), no hago concatenación con variables para
    // evitar SQLInyections
    const char *updateQuery = "UPDATE trial SET \"heatMapVideoPath\"=$1 WHERE id=$2;";

    // Definir los valores de los parámetros
    const char *paramValues[2] = {url.c_str(), trialId.c_str()};

    // Ejecutar la consulta con parámetros
    result = PQexecParams(cnn, updateQuery, 2, NULL, paramValues, NULL, NULL, 0);

    // Verificar si la consulta se ejecutó correctamente
    if (!result || PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        cerr << "Error al actualizar datos: " << PQerrorMessage(cnn) << endl;
        if (result)
            PQclear(result);
        PQfinish(cnn); // Cerrar la conexión
        return;
    }

    cout << "Datos actualizados correctamente." << endl;

    // Liberar memoria y cerrar conexión
    PQclear(result);
    PQfinish(cnn);
}

// Definir MKDIR por si la carpeta de salida de video no existe
void createDirectoryIfNotExists(const string &path)
{
    struct stat info;

    if (stat(path.c_str(), &info) == 0) // Si la ruta existe
    {
        if (info.st_mode & S_IFDIR)
        {
            cout << "La carpeta ya existe: " << path << endl;
        }
        else
        {
            cerr << "Existe un archivo con el mismo nombre: " << path << endl;
        }
        return;
    }

    // Si no existe, intentamos crearla
    if (MKDIR(path.c_str()) == 0)
    {
        cout << "Carpeta creada: " << path << endl;
    }
    else
    {
        cerr << "Error al crear la carpeta: " << path << endl;
    }
}

void fetchCSVAndGenerateAnimation(const vector<pair<double, double>> &coords_left,
                                  const vector<pair<double, double>> &coords_right,
                                  const string &wearableId_L,
                                  const string &wearableId_R,
                                  const string &experimentId,
                                  const string &participantId,
                                  const string &swId,
                                  const string &trialId)
{
    // 1) Cargar coords (JSON)
    std::vector<std::vector<int>> pressures_left, pressures_right;

    // 2) Crear la carpeta de salida dentro del volumen, si no existe
    createDirectoryIfNotExists("/app/backend/videos/hm_videos");

    // Construir la ruta absoluta para el video dentro del volumen compartido
    std::string videoPath = "/app/backend/videos/hm_videos/experimentId_" + experimentId +
                            "_participantId_" + participantId +
                            "_trialId_" + trialId +
                            "_sWId_" + swId +
                            "_wearableL_" + wearableId_L +
                            "_wearableR_" + wearableId_R + ".mp4";

    fetchCSV(baseUrl, experimentId, participantId, swId, trialId, wearableId_L, wearableId_R, pressures_left, pressures_right);
    generate_combined_animation_JET(
        pressures_left,
        pressures_right,
        coords_left,
        coords_right,
        videoPath);

    updateTrial(videoPath, trialId);
}
//------------------------------------------------------------
// 119) Eliminar los datos de la cola que ya han sido procesados
//------------------------------------------------------------

void deleteFromQueue(redisContext *context, const std::string &lastID)
{
    // Eliminar el mensaje de la cola
    redisReply *delReply = (redisReply *)redisCommand(
        context,
        "XDEL %s %s",
        redisQueue.c_str(),
        lastID.c_str());
    if (delReply)
        freeReplyObject(delReply);
}

//------------------------------------------------------------
// 12) Procesar el Stream del mensaje de la cola
//------------------------------------------------------------

void processStream(redisReply *stream, redisContext *context, std::string &lastID, const vector<pair<double, double>> &coords_left, const vector<pair<double, double>> &coords_right)
{
    if (stream->type == REDIS_REPLY_ARRAY && stream->elements >= 2)
    {
        redisReply *streamName = stream->element[0];
        redisReply *messages = stream->element[1];

        for (size_t j = 0; j < messages->elements; j++)
        {
            redisReply *message = messages->element[j];

            if (message->type == REDIS_REPLY_ARRAY && message->elements >= 2)
            {
                redisReply *msgID = message->element[0];
                redisReply *msgData = message->element[1];

                string wearableIdL, wearableIdR, experimentId, participantId, sWId, trialId;

                if (msgID->type == REDIS_REPLY_STRING)
                {
                    lastID = msgID->str; // Guardamos el último ID leído
                    std::cout << "📩 Mensaje recibido (" << lastID << "): ";

                    processMessage(msgData, wearableIdL, wearableIdR, experimentId, participantId, sWId, trialId);

                    // Imprimir el par de valores
                    std::cout << "wearableId_L: " << wearableIdL << ", wearableId_R: " << wearableIdR
                              << ", experimentId: " << experimentId << ", participantId: " << participantId
                              << ", sWId: " << sWId << ", trialId: " << trialId << std::endl;

                    fetchCSVAndGenerateAnimation(coords_left, coords_right, wearableIdL, wearableIdR, experimentId, participantId, sWId, trialId);

                    // Eliminar el mensaje de la cola
                    deleteFromQueue(context, lastID);
                }
            }
        }
    }
}

void consumeFromQueue(const std::string &redisQueue)
{
    vector<pair<double, double>> coords_left, coords_right;
    readCoordinates(coords_left, coords_right);
    // redisContext *context = connectToRedis("redis_container", 6379);
    redisContext *context = connectToRedis("redis_container", 6379, "mi_contraseña_secreta");
    if (context == nullptr)
        return;

    std::string lastID = "0"; // Comenzar desde el inicio

    while (true)
    {
        // Verificar si sigue activa la conexión
        if (context == nullptr || context->err)
        {
            cerr << "❌ La conexión con Redis se perdió. Saliendo..." << endl;
            break;
        }

        cout << "🔍 Buscando mensajes en la cola..." << std::endl;
        // Método de espera pasiva para los contenidos de la cola
        redisReply *reply = readFromStream(context, lastID);

        if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements > 0)
        {
            for (size_t i = 0; i < reply->elements; i++)
            {
                redisReply *stream = reply->element[i];
                processStream(stream, context, lastID, coords_left, coords_right);
            }
        }

        if (reply)
        {
            freeReplyObject(reply);
        }
    }

    redisFree(context);
}

//------------------------------------------------------------
// 8) MAIN de ejemplo
//------------------------------------------------------------
int main()
{
    consumeFromQueue(redisQueue);

    return 0;
}
