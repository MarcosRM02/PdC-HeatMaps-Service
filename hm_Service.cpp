#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <thread>
#include <cstdio>    // popen, pclose
#include <algorithm> // std::min, std::max
#include <deque>     // para trails COP
#include <mutex>
#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0777)

#include <opencv2/opencv.hpp>
#include <rapidjson/document.h>
#include <hiredis/hiredis.h>
#include <curl/curl.h>
#include <libpq-fe.h>

using namespace std;
using namespace cv;
using namespace rapidjson;

const std::string apiHost = "http://ssith-backend-container:3000";

// Parámetros de vídeo y malla
constexpr int wFinal = 175;
constexpr int hFinal = 520;
constexpr int gridW = 20;
constexpr int gridH = 69;
constexpr double radius = 70.0;
constexpr double smoothness = 2.0;
constexpr double fps = 32.0;
constexpr int trailLength = 10;
constexpr int margin = 50;
constexpr int legendWidth = 80;
const string baseUrl = apiHost + "/swData/generateCSV/";
const string redisQueue = "redis_queue";

// PostgreSQL
PGconn *cnn = nullptr;
PGresult *result = nullptr;
const char *dbHost = "sqlDB";
const char *dbPort = "5432";
const char *dbName = "ssith-db";
const char *dbUser = "admin";
const char *dbPass = "admin";

// API login (JWT)
const string apiUser = "marcos";
const string apiPassword = "zodv38jN0Bty5ns1";
const string loginUrl = apiHost + "/authentication/serviceLogin/";

// CSRF token
static string g_csrfToken;
const string csrfUrl = apiHost + "/authentication/csrf-token";
const string trialsBaseUrl = apiHost + "/trials/edit/";
const string rVideoPathBaseUrl = apiHost + "/trials/getRVideoPath/";

// cURL write callback
size_t WriteCallback(void *contents, size_t size, size_t nmemb, string *output)
{
    size_t total = size * nmemb;
    output->append((char *)contents, total);
    return total;
}

// Realizar login y obtener token JWT
string loginToAPI()
{
    // Borra el cookies.txt anterior (si existía)
    std::remove("cookies.txt");
    CURL *curl = curl_easy_init();
    string response;
    if (!curl)
        return response;

    // Enable cookie engine: store any Set-Cookie in memory…
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    // …and also write cookies out to disk (optional)
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "cookies.txt");

    string jsonData = "{\"user\":\"" + apiUser + "\",\"password\":\"" + apiPassword + "\"}";
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, loginUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // 3) Perform
    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    // 4) Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // 5) Error handling
    if (res != CURLE_OK)
    {
        std::cerr << "Error cURL login: " << curl_easy_strerror(res) << "\n";
        std::exit(EXIT_FAILURE);
    }
    // aquí permitimos 200 **o** 201
    if (httpCode != 200 && httpCode != 201)
    {
        std::cerr
            << "Login fallido: HTTP " << httpCode
            << ", body=" << response << "\n";
        std::exit(EXIT_FAILURE);
    }

    // 6) Éxito
    return response;
}

// Obtener token (y cachear)
string &getToken()
{
    static string token;
    if (token.empty())
    {
        token = loginToAPI();
    }
    return token;
}

//------------------------------------------------------------
// Leer coordenadas JSON
//------------------------------------------------------------
void read_coordinates(const string &filename, vector<pair<double, double>> &coords)
{
    ifstream file(filename);
    if (!file.is_open())
    {
        cerr << "No se pudo abrir " << filename << endl;
        return;
    }
    stringstream buffer;
    buffer << file.rdbuf();
    Document doc;
    doc.Parse(buffer.str().c_str());
    if (doc.HasParseError() || !doc.IsArray())
    {
        cerr << "Error parseando " << filename << endl;
        return;
    }
    for (SizeType i = 0; i < doc.Size(); i++)
    {
        if (doc[i].HasMember("x") && doc[i].HasMember("y") &&
            doc[i]["x"].IsDouble() && doc[i]["y"].IsDouble())
        {
            coords.emplace_back(doc[i]["x"].GetDouble(), doc[i]["y"].GetDouble());
        }
    }
}

//------------------------------------------------------------
// HTTP GET genérico
//------------------------------------------------------------
string fetchUrlContent(const string &url)
{
    CURL *curl = curl_easy_init();
    string readBuffer;
    if (!curl)
        return readBuffer;

    // 1) Enable cookie handling: send any stored cookies (including jwt)
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "cookies.txt");
    // (Optional) keep cookies updated on disk
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "cookies.txt");

    // 2) Standard headers
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // 3) Set request URL and headers
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // 4) Capture response body
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    // 5) Perform request
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        cerr << "Error cURL GET: " << curl_easy_strerror(res) << endl;
    }

    // 6) Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return readBuffer;
}

//------------------------------------------------------------
// Extraer contenido interno removiendo [[ ]]
//------------------------------------------------------------
string extractContent(const string &raw)
{
    if (raw.size() < 4 || raw.substr(0, 2) != "[[" || raw.substr(raw.size() - 2) != "]]")
    {
        cerr << "Formato inesperado del API" << endl;
        return string();
    }
    return raw.substr(2, raw.size() - 4);
}

//------------------------------------------------------------
// Separar CSVs izquierda/derecha
//------------------------------------------------------------
pair<string, string> splitCSVContent(const string &content)
{
    size_t pos = content.find("],[");
    if (pos == string::npos)
        pos = content.find("],");
    if (pos == string::npos)
        return {"", ""};
    string left = content.substr(0, pos);
    string right;
    if (content.substr(pos, 3) == "],[")
        right = content.substr(pos + 3);
    else
        right = content.substr(pos + 2);
    return {left, right};
}

//------------------------------------------------------------
// Procesar parte del JSON a CSV
//------------------------------------------------------------
string processPart(const string &part)
{
    string result;
    size_t pos = 0;
    bool first = true;
    while (pos < part.size())
    {
        size_t s = part.find('"', pos);
        if (s == string::npos)
            break;
        size_t e = part.find('"', s + 1);
        if (e == string::npos)
            break;
        string row = part.substr(s + 1, e - s - 1);
        if (!first)
            result += "\n";
        result += row;
        first = false;
        pos = e + 1;
    }
    return result;
}

//------------------------------------------------------------
// Parse CSV string a vector<vector<int>>
//------------------------------------------------------------
vector<vector<int>> parseCSV(const string &csvData)
{
    vector<vector<int>> data;
    stringstream ss(csvData);
    string line;
    while (getline(ss, line))
    {
        vector<int> row;
        stringstream ls(line);
        string val;
        while (getline(ls, val, ','))
        {
            try
            {
                row.push_back(stoi(val));
            }
            catch (...)
            {
                row.push_back(0);
            }
        }
        data.push_back(row);
    }
    return data;
}

//------------------------------------------------------------
// Fetch y parse CSVs de API
//------------------------------------------------------------
void fetchCSV(
    const string &expId,
    const string &partId,
    const string &sWId,
    const string &trialId,
    const string &wearL,
    const string &wearR,
    vector<vector<int>> &leftData,
    vector<vector<int>> &rightData)
{
    string query = "wearableIds=" + wearL + "&wearableIds=" + wearR;
    string url = baseUrl + expId + "/" + partId + "/" + sWId + "/" + trialId + "?" + query;
    string raw = fetchUrlContent(url);
    string content = extractContent(raw);
    auto parts = splitCSVContent(content);
    leftData = parseCSV(processPart(parts.first));
    rightData = parseCSV(processPart(parts.second));
}

//------------------------------------------------------------
// Conexión a Redis
//------------------------------------------------------------
redisContext *connectToRedis(const string &host, int port, const string &pass)
{
    redisContext *ctx = redisConnect(host.c_str(), port);
    if (!ctx || ctx->err)
    {
        cerr << "Error Redis connect: " << (ctx ? ctx->errstr : "") << endl;
        return nullptr;
    }
    if (!pass.empty())
    {
        redisReply *reply = (redisReply *)redisCommand(ctx, "AUTH %s", pass.c_str());
        if (!reply || reply->type == REDIS_REPLY_ERROR)
        {
            cerr << "Redis AUTH error: " << (reply ? reply->str : "") << endl;
            if (reply)
                freeReplyObject(reply);
            redisFree(ctx);
            return nullptr;
        }
        freeReplyObject(reply);
    }
    return ctx;
}

redisReply *readFromStream(redisContext *ctx, const string &lastID)
{
    return (redisReply *)redisCommand(ctx, "XREAD BLOCK 0 STREAMS %s %s", redisQueue.c_str(), lastID.c_str());
}

//------------------------------------------------------------
// Procesar mensaje Redis
//------------------------------------------------------------
void processMessage(redisReply *data,
                    string &wearL, string &wearR,
                    string &expId, string &partId,
                    string &sWId, string &trialId)
{
    for (size_t i = 0; i < data->elements; i += 2)
    {
        redisReply *field = data->element[i];
        redisReply *val = data->element[i + 1];
        if (field->type == REDIS_REPLY_STRING && val->type == REDIS_REPLY_STRING)
        {
            string f = field->str;
            string v = val->str;
            if (f == "wearableId_L")
                wearL = v;
            else if (f == "wearableId_R")
                wearR = v;
            else if (f == "experimentId")
                expId = v;
            else if (f == "participantId")
                partId = v;
            else if (f == "sWId")
                sWId = v;
            else if (f == "trialId")
                trialId = v;
        }
    }
}

//------------------------------------------------------------
// Leer coords JSON una vez
//------------------------------------------------------------
void readCoordinates(vector<pair<double, double>> &cl, vector<pair<double, double>> &cr)
{
    read_coordinates("./in/leftPoints.json", cl);
    read_coordinates("./in/rightPoints.json", cr);
}

//------------------------------------------------------------
// PostgreSQL: conectar y update
//------------------------------------------------------------
void createConnection()
{
    cnn = PQsetdbLogin(dbHost, dbPort, nullptr, nullptr, dbName, dbUser, dbPass);
    if (!cnn || PQstatus(cnn) == CONNECTION_BAD)
    {
        cerr << "Postgres connect error: " << (cnn ? PQerrorMessage(cnn) : "") << endl;
        if (cnn)
            PQfinish(cnn);
        cnn = nullptr;
    }
}

void fetchCsrfToken()
{
    if (!g_csrfToken.empty())
        return; // ya lo tenemos

    std::string response;
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        std::cerr << "CURL init failed (CSRF)\n";
        std::exit(EXIT_FAILURE);
    }

    curl_easy_setopt(curl, CURLOPT_URL, csrfUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "cookies.txt");
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "cookies.txt");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || httpCode != 200)
    {
        std::cerr << "Failed to fetch CSRF token: HTTP "
                  << httpCode << ", body=" << response << "\n";
        std::exit(EXIT_FAILURE);
    }

    // parse JSON { "csrfToken":"XYZ" }
    Document doc;
    doc.Parse(response.c_str());
    if (!doc.IsObject() || !doc.HasMember("csrfToken") || !doc["csrfToken"].IsString())
    {
        std::cerr << "Invalid CSRF response: " << response << "\n";
        std::exit(EXIT_FAILURE);
    }
    g_csrfToken = doc["csrfToken"].GetString();
}

void updateTrial(const std::string &videoPath, const std::string &trialId)
{
    std::string url = trialsBaseUrl + trialId;
    std::string bodyJson = "{\"heatMapVideoPath\":\"" + videoPath + "\"}";

    CURL *curl = curl_easy_init();
    if (!curl)
        return;

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string csrfHeader = "X-CSRF-Token: " + g_csrfToken;
    headers = curl_slist_append(headers, csrfHeader.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyJson.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "cookies.txt");
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "cookies.txt");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);

    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || httpCode < 200 || httpCode >= 300)
    {
        std::cerr << "PUT /trials/" << trialId
                  << " failed: HTTP " << httpCode
                  << ", body=" << response << "\n";
    }
    else
    {
        std::cout << "updateTrial OK: " << response << "\n";
    }
}

//------------------------------------------------------------
// Eliminar mensaje Redis procesado
//------------------------------------------------------------
void deleteFromQueue(redisContext *ctx, const string &lastID)
{
    redisReply *r = (redisReply *)redisCommand(ctx, "XDEL %s %s", redisQueue.c_str(), lastID.c_str());
    if (r)
        freeReplyObject(r);
}

//------------------------------------------------------------
// Generar heatmap + JET
//------------------------------------------------------------
Mat generate_heatmap_jet(const vector<pair<double, double>> &coords,
                         const vector<int> &pressures,
                         int widthF, int heightF,
                         int gW, int gH,
                         double rad, double smooth)
{
    vector<vector<double>> Z(gH, vector<double>(gW, 0.0));
    for (size_t i = 0; i < coords.size(); ++i)
    {
        double x0 = coords[i].first;
        double y0 = coords[i].second;
        double p0 = pressures[i];
        for (int gy = 0; gy < gH; ++gy)
        {
            double Yg = (gy + 0.5) / double(gH) * heightF;
            for (int gx = 0; gx < gW; ++gx)
            {
                double Xg = (gx + 0.5) / double(gW) * widthF;
                double dx = Xg - x0;
                double dy = Yg - y0;
                double d2 = dx * dx + dy * dy;
                Z[gy][gx] += p0 * exp(-smooth * (d2 / (rad * rad)));
            }
        }
    }
    for (int gy = 0; gy < gH; ++gy)
    {
        for (int gx = 0; gx < gW; ++gx)
        {
            Z[gy][gx] = min(max(Z[gy][gx], 0.0), 4095.0);
        }
    }
    Mat gray(heightF, widthF, CV_8UC1);
    for (int y = 0; y < heightF; ++y)
    {
        int gy = min(int(double(y) / heightF * gH), gH - 1);
        uchar *row = gray.ptr<uchar>(y);
        for (int x = 0; x < widthF; ++x)
        {
            int gx = min(int(double(x) / widthF * gW), gW - 1);
            int v = int(Z[gy][gx] * (255.0 / 4095.0));
            row[x] = uchar(min(max(v, 0), 255));
        }
    }
    Mat color;
    applyColorMap(gray, color, COLORMAP_JET);
    return color;
}

//------------------------------------------------------------
// Dibujar índices
//------------------------------------------------------------
void draw_indices(Mat &img, const vector<pair<double, double>> &coords)
{
    for (size_t i = 0; i < coords.size(); ++i)
    {
        Point p(int(coords[i].first), int(coords[i].second));
        circle(img, p, 5, Scalar(0, 0, 0), -1);
        putText(img, to_string(i), p + Point(5, -5), FONT_HERSHEY_SIMPLEX, 0.4, Scalar(255, 255, 255), 1, LINE_AA);
    }
}

//------------------------------------------------------------
// Crear colorbar JET
//------------------------------------------------------------
Mat create_colorbar(int width, int height)
{
    Mat grad(height, width, CV_8UC1);
    for (int y = 0; y < height; ++y)
    {
        uchar val = uchar(255.0 * (1.0 - double(y) / (height - 1)));
        for (int x = 0; x < width; ++x)
            grad.at<uchar>(y, x) = val;
    }
    Mat bar;
    applyColorMap(grad, bar, COLORMAP_JET);
    return bar;
}

//------------------------------------------------------------
// Anotar ticks en colorbar
//------------------------------------------------------------
void annotate_cb(Mat &colorBar,
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
// Cálculo COP
//------------------------------------------------------------
Point compute_COP(const vector<int> &press, const vector<pair<double, double>> &coords)
{
    double sum = 0, x = 0, y = 0;
    for (size_t i = 0; i < press.size(); ++i)
    {
        sum += press[i];
        x += coords[i].first * press[i];
        y += coords[i].second * press[i];
    }
    if (sum <= 0)
        return Point(0, 0);
    return Point(int(x / sum), int(y / sum));
}

//------------------------------------------------------------
// Generar animación completa: heatmap + COP
//------------------------------------------------------------
void generate_animation(const vector<vector<int>> &pl,
                        const vector<vector<int>> &pr,
                        const vector<pair<double, double>> &cl,
                        const vector<pair<double, double>> &cr,
                        const string &outFile)
{

    // 1) Calcular dimensiones del frame completo con fondo blanco
    int finalWidth = (wFinal * 2) + legendWidth + (margin * 3);
    int finalHeight = hFinal + (margin * 2);

    // 2) Crear la barra de colores
    int barWidth = 20;
    int barHeight = hFinal - 20; // Reducir la altura de la barra de colores
    Mat colorBar = create_colorbar(barWidth, barHeight);

    // Contenedor con fondo blanco para la barra de colores
    Mat legendContainer(barHeight, legendWidth, CV_8UC3, Scalar(255, 255, 255));
    Rect roiBar(10, 0, barWidth, barHeight);
    colorBar.copyTo(legendContainer(roiBar));

    //  Agregar etiquetas de escala
    vector<int> tickVals = {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000}; //  vector<int> tickVals = {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000};
    annotate_cb(legendContainer, 10 + barWidth, tickVals);

    string cmd = "ffmpeg -y -f rawvideo -pixel_format bgr24 -video_size " + to_string(finalWidth) + "x" + to_string(finalHeight) +
                 " -framerate " + to_string(fps) + " -i pipe:0 -c:v libx264 -preset fast -crf 28 -pix_fmt yuv420p \"" + outFile + "\"";
    FILE *pipe = popen(cmd.c_str(), "w");
    if (!pipe)
    {
        cerr << "FFmpeg error" << endl;
        return;
    }

    deque<Point> trailL, trailR;
    size_t frames = min(pl.size(), pr.size());
    for (size_t i = 0; i < frames; ++i)
    {
        Mat L = generate_heatmap_jet(cl, pl[i], wFinal, hFinal, gridW, gridH, radius, smoothness);
        Mat R = generate_heatmap_jet(cr, pr[i], wFinal, hFinal, gridW, gridH, radius, smoothness);
        draw_indices(L, cl);
        draw_indices(R, cr);
        Point cL = compute_COP(pl[i], cl), cR = compute_COP(pr[i], cr);
        trailL.push_back(cL);
        if (trailL.size() > trailLength)
            trailL.pop_front();
        trailR.push_back(cR);
        if (trailR.size() > trailLength)
            trailR.pop_front();

        Mat F(finalHeight, finalWidth, CV_8UC3, Scalar(255, 255, 255));
        L.copyTo(F(Rect(margin, margin, wFinal, hFinal)));
        R.copyTo(F(Rect(margin + wFinal + margin, margin, wFinal, hFinal)));
        legendContainer.copyTo(F(Rect(margin + wFinal + margin + wFinal + margin - 22, margin + 19, legendWidth, hFinal - 20)));

        for (size_t j = 0; j < trailL.size(); ++j)
        {
            double alpha = double(j + 1) / trailL.size();
            int I = int(255.0 * alpha);
            int rad = max(1, int(5 * alpha)); // radio decreciente: mínimo 1px
            Point pL = trailL[j] + Point(margin, margin);
            circle(F, pL, rad, Scalar(I, 0, I), -1, LINE_AA);
        }
        for (size_t j = 0; j < trailR.size(); ++j)
        {
            double alpha = double(j + 1) / trailL.size();
            int I = int(255.0 * (j + 1) / trailR.size());
            int rad = max(1, int(5 * alpha)); // radio decreciente: mínimo 1px
            circle(F, trailR[j] + Point(margin + wFinal + margin, margin), rad, Scalar(I, 0, I), -1, LINE_AA);
        }
        fwrite(F.data, 1, F.total() * F.elemSize(), pipe);
    }
    pclose(pipe);
    cout << "Animación generada: " << outFile << endl;
}

// ---------------------------------------------------

// Obtener duración de un video con ffprobe
double getVideoDuration(const string &videoPath)
{
    string cmd = "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"" + videoPath + "\"";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe)
    {
        cerr << "Error al ejecutar ffprobe" << endl;
        return -1.0;
    }

    char buffer[128];
    string result = "";
    while (!feof(pipe))
    {
        if (fgets(buffer, 128, pipe) != NULL)
            result += buffer;
    }
    pclose(pipe);

    try
    {
        result.erase(remove_if(result.begin(), result.end(), ::isspace), result.end());
        return stod(result);
    }
    catch (const std::exception &e)
    {
        cerr << "Error al convertir duración del video: " << e.what() << endl;
        return -1.0;
    }
}

// Recortar un video a una duración específica
bool trimVideo(const string &inputPath, const string &outputPath, double duration)
{
    string cmd = "ffmpeg -y -i \"" + inputPath + "\" -t " + to_string(duration) + " -c copy \"" + outputPath + "\"";
    cout << "Ejecutando: " << cmd << endl;
    int result = system(cmd.c_str());
    return result == 0;
}

// Obtener la ruta del video original grabado
string getOriginalVideoPath(const string &trialId)
{
    // Aquí deberías obtener la ruta del video original a través de la API o DB
    string url = rVideoPathBaseUrl + trialId;
    string response = fetchUrlContent(url);

    if (response.empty() || response == "null")
    {
        cout << "No hay video original para el trial " << trialId << endl;
        return "";
    }

    // Eliminar comillas si existen
    if (response.front() == '"' && response.back() == '"')
    {
        response = response.substr(1, response.length() - 2);
    }

    return response;
}

// Hacer coincidir las duraciones de dos videos, recortando el más largo
void matchVideoLengths(const string &heatmapVideo, const string &originalVideo)
{
    double durationHeatmap = getVideoDuration(heatmapVideo);
    double durationOriginal = getVideoDuration(originalVideo);

    if (durationHeatmap <= 0 || durationOriginal <= 0)
    {
        cerr << "No se pudo obtener la duración de uno o ambos videos" << endl;
        return;
    }

    cout << "Duraciones: Heatmap=" << durationHeatmap << "s, Original=" << durationOriginal << "s" << endl;

    // Si la diferencia es muy pequeña, no recortar
    if (abs(durationHeatmap - durationOriginal) < 0.1)
    {
        return;
    }

    double shortestDuration = min(durationHeatmap, durationOriginal);

    if (durationHeatmap > shortestDuration)
    {
        // El video de heatmap es más largo, lo recortamos
        string trimmedHeatmap = heatmapVideo.substr(0, heatmapVideo.find_last_of('.')) + "_trimmed.mp4";
        cout << "Recortando heatmap video a " << shortestDuration << "s" << endl;
        if (trimVideo(heatmapVideo, trimmedHeatmap, shortestDuration))
        {
            // Reemplazar el archivo original con el recortado
            string mvCmd = "mv \"" + trimmedHeatmap + "\" \"" + heatmapVideo + "\"";
            system(mvCmd.c_str());
            cout << "Heatmap recortado exitosamente" << endl;
        }
    }
    else
    {
        // El video original es más largo, lo recortamos
        string trimmedOriginal = originalVideo.substr(0, originalVideo.find_last_of('.')) + "_trimmed.mp4";
        cout << "Recortando video original a " << shortestDuration << "s" << endl;
        if (trimVideo(originalVideo, trimmedOriginal, shortestDuration))
        {
            // Reemplazar el archivo original con el recortado
            string mvCmd = "mv \"" + trimmedOriginal + "\" \"" + originalVideo + "\"";
            system(mvCmd.c_str());
            cout << "Video original recortado exitosamente" << endl;
        }
    }
}
//------------------------------------------------------------
// Procesar stream Redis
//------------------------------------------------------------
void processStream(redisReply *stream, redisContext *ctx, string &lastID,
                   const vector<pair<double, double>> &cl,
                   const vector<pair<double, double>> &cr)
{
    if (!stream || stream->type != REDIS_REPLY_ARRAY || stream->elements < 2)
        return;
    redisReply *messages = stream->element[1];
    for (size_t i = 0; i < messages->elements; ++i)
    {
        redisReply *msg = messages->element[i];
        if (!msg || msg->type != REDIS_REPLY_ARRAY || msg->elements < 2)
            continue;
        redisReply *msgID = msg->element[0];
        redisReply *data = msg->element[1];
        if (msgID->type != REDIS_REPLY_STRING)
            continue;
        lastID = msgID->str;
        string wearL, wearR, expId, partId, sWId, trialId;
        processMessage(data, wearL, wearR, expId, partId, sWId, trialId);
        vector<vector<int>> pl, pr;
        fetchCSV(expId, partId, sWId, trialId, wearL, wearR, pl, pr);
        MKDIR("/app/backend/videos/hm_videos");
        string basePath = "/app/backend/videos/hm_videos/experimentId_" + expId +
                          "_participantId_" + partId +
                          "_trialId_" + trialId +
                          "_sWId_" + sWId +
                          "_wearableL_" + wearL +
                          "_wearableR_" + wearR;
        string outFile = basePath + ".mp4";
        generate_animation(pl, pr, cl, cr, outFile);

        // ----------------------------
        string originalVideo = getOriginalVideoPath(trialId);
        if (!originalVideo.empty())
        {
            cout << "Video original encontrado: " << originalVideo << endl;
            matchVideoLengths(outFile, originalVideo);
        }
        else
        {
            cout << "No se encontró video original para comparar" << endl;
        }
        // ----------------------------
        updateTrial(outFile, trialId);
        deleteFromQueue(ctx, lastID);
    }
}

//------------------------------------------------------------
// Consumir cola Redis
//------------------------------------------------------------
void consumeFromQueue()
{
    vector<pair<double, double>> coordsL, coordsR;
    readCoordinates(coordsL, coordsR);
    redisContext *ctx = connectToRedis("redis_container", 6379, "mi_contraseña_secreta");
    if (!ctx)
        return;
    string lastID = "0";
    while (true)
    {
        if (ctx->err)
            break;
        cout << "Esperando mensajes..." << endl;
        redisReply *reply = readFromStream(ctx, lastID);
        if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements > 0)
        {
            for (size_t i = 0; i < reply->elements; ++i)
            {
                processStream(reply->element[i], ctx, lastID, coordsL, coordsR);
            }
        }
        if (reply)
            freeReplyObject(reply);
    }
    redisFree(ctx);
}

//------------------------------------------------------------
// MAIN
//------------------------------------------------------------
int main()
{
    cout << "Logging in to service..." << endl;
    loginToAPI();
    fetchCsrfToken();
    cout << "Iniciando generación de animaciones..." << endl;
    consumeFromQueue();
    return 0;
}
