#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <curl/curl.h>

std::string WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

std::string buildWearableIdsQuery(const std::vector<std::string> &wearableIds)
{
    std::string query;
    for (const auto &id : wearableIds)
    {
        if (!query.empty())
            query += "&";
        query += "wearableIds[]=" + id;
    }
    return query;
}

std::vector<std::vector<double>> parseCSV(const std::string &csvData)
{
    std::vector<std::vector<double>> data;
    std::stringstream ss(csvData);
    std::string line;
    while (std::getline(ss, line))
    {
        std::vector<double> row;
        std::stringstream lineStream(line);
        std::string value;
        while (std::getline(lineStream, value, ','))
        {
            try
            {
                row.push_back(std::stod(value)); // Convertir cada valor a double
            }
            catch (...)
            {
                row.push_back(0.0); // Si no puede convertir, agregar 0.0
            }
        }
        data.push_back(row);
    }
    return data;
}

std::pair<std::vector<std::vector<double>>, std::vector<std::vector<double>>> fetchCSV(const std::string &baseUrl, const std::string &experimentId,
                                                                                       const std::string &participantId, const std::string &swId,
                                                                                       const std::string &trialId, const std::vector<std::string> &wearableIds)
{
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    // Construcción de la URL
    std::string wearableIdsQuery = buildWearableIdsQuery(wearableIds);
    std::string url = baseUrl + "swData/generateCSV/" + experimentId + "/" + participantId + "/" + swId + "/" + trialId + "?" + wearableIdsQuery;

    curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Ejecutar la solicitud
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            std::cerr << "❌ Error en la petición HTTP: " << curl_easy_strerror(res) << std::endl;
        }

        curl_easy_cleanup(curl);
    }

    // Aquí asumimos que la respuesta es un string[][], donde cada subarray es un CSV
    // Ejemplo:
    // readBuffer = "[\"col1,col2,col3\n1.1,2.2,3.3\n4.4,5.5,6.6\",\"col1,col2,col3\n7.7,8.8,9.9\n10.10,11.11,12.12\"]"

    // Extraemos ambos CSV: uno en la posición [0] y otro en la posición [1]
    size_t firstCommaPos = readBuffer.find("\",\"") + 3;                                     // Encuentra la posición del primer separador entre CSVs
    size_t secondCommaPos = readBuffer.rfind("\",\"");                                       // Encuentra la posición del segundo separador entre CSVs
    std::string leftCsv = readBuffer.substr(1, firstCommaPos - 3);                           // Substring del primer CSV
    std::string rightCsv = readBuffer.substr(firstCommaPos, secondCommaPos - firstCommaPos); // Substring del segundo CSV

    // Convertir ambos CSV a vector<vector<double>>
    std::vector<std::vector<double>> pressures_left = parseCSV(leftCsv);
    std::vector<std::vector<double>> pressures_right = parseCSV(rightCsv);

    return {pressures_left, pressures_right};
}

int main()
{
    std::string baseUrl = "http://localhost:3000/";
    std::string experimentId = "exp123";
    std::string participantId = "part456";
    std::string swId = "sw789";
    std::string trialId = "trial001";
    std::vector<std::string> wearableIds = {"wearable1", "wearable2"};

    // Llamar a la función fetchCSV
    auto [pressures_left, pressures_right] = fetchCSV(baseUrl, experimentId, participantId, swId, trialId, wearableIds);

    // Mostrar el resultado
    std::cout << "Presiones Left:" << std::endl;
    for (const auto &row : pressures_left)
    {
        for (const auto &val : row)
        {
            std::cout << val << " ";
        }
        std::cout << std::endl;
    }

    std::cout << "Presiones Right:" << std::endl;
    for (const auto &row : pressures_right)
    {
        for (const auto &val : row)
        {
            std::cout << val << " ";
        }
        std::cout << std::endl;
    }

    return 0;
}
