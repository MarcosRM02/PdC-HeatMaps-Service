#include <iostream>
#include <curl/curl.h>
#include <vector>
#include <sstream>

// Callback para almacenar la respuesta del servidor
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *output)
{
    size_t total_size = size * nmemb;
    output->append((char *)contents, total_size);
    return total_size;
}

// Función para convertir un vector de IDs en la estructura correcta de Query Params
std::string buildWearableIdsQuery(const std::vector<std::string> &wearableIds)
{
    std::ostringstream oss;
    for (size_t i = 0; i < wearableIds.size(); ++i)
    {
        if (i > 0)
            oss << "&"; // Agrega '&' entre cada parámetro
        oss << "wearableIds=" << wearableIds[i];
    }
    return oss.str();
}

// Función para hacer la petición HTTP GET
std::string fetchCSV(const std::string &baseUrl, const std::string &experimentId,
                     const std::string &participantId, const std::string &swId,
                     const std::string &trialId, const std::vector<std::string> &wearableIds)
{
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    // Construcción correcta de los parámetros en la URL
    std::string wearableIdsQuery = buildWearableIdsQuery(wearableIds);
    std::string url = baseUrl + "swData/generateCSV/" + experimentId + "/" + participantId + "/" + swId + "/" + trialId + "?" + wearableIdsQuery;

    curl = curl_easy_init();
    if (curl)
    {
        // Configurar la URL
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Ejecutar la solicitud
        res = curl_easy_perform(curl);

        if (res != CURLE_OK)
        {
            std::cerr << "❌ Error en la petición HTTP: " << curl_easy_strerror(res) << std::endl;
        }

        // Liberar memoria
        curl_easy_cleanup(curl);
    }

    return readBuffer; // Devuelve la respuesta del servidor (CSV en formato string)
}

int main()
{
    std::string baseUrl = "http://localhost:3000/"; // Cambia esto por la IP/host de tu servidor -> Cambiar por el nombre del contenedor cuando lo docekerice
    std::string experimentId = "1";
    std::string participantId = "1";
    std::string swId = "1";
    std::string trialId = "1";

    // Lista de wearables en el formato correcto
    std::vector<std::string> wearableIds = {"2", "1"};

    // Hacer la petición y recibir la respuesta
    std::string response = fetchCSV(baseUrl, experimentId, participantId, swId, trialId, wearableIds);

    std::cout << "📩 Respuesta del servidor:\n"
              << response << std::endl;

    return 0;
}
