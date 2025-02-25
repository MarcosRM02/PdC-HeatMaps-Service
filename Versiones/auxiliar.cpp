// ------------------------------------------------------------------

// Función para conectarse a Redis y obtener los datos CSV

/*
Este método, de momento no sirve de mucho, ya que de redis obtendre solo los id popara sacar
los csv de la peticion http dentro de los endpoints de la api
*/
vector<vector<double>> getCSVFromRedis(const string &key, const string &redis_host = "localhost", int redis_port = 6379) //&redis_host = "redis_container",
{
    redisContext *c = redisConnect(redis_host.c_str(), redis_port);
    if (c == NULL || c->err)
    {
        if (c)
        {
            cerr << "Error: " << c->errstr << endl;
            redisFree(c);
        }
        else
        {
            cerr << "Error: No se pudo asignar el contexto Redis" << endl;
        }
        exit(1);
    }

    // Ejecutar comando GET en Redis
    redisReply *reply = (redisReply *)redisCommand(c, "GET %s", key.c_str());
    if (reply == NULL || reply->type == REDIS_REPLY_NIL)
    {
        cerr << "Error: No se encontró la clave " << key << " en Redis." << endl;
        redisFree(c);
        return {};
    }

    // Convertir la respuesta de Redis en una cadena CSV
    string csv_data(reply->str);
    freeReplyObject(reply);
    redisFree(c);

    // Convertir CSV en matriz de datos
    vector<vector<double>> data;
    istringstream stream(csv_data);
    string line;

    while (getline(stream, line))
    {
        vector<double> row;
        istringstream lineStream(line);
        string value;

        while (getline(lineStream, value, ','))
        {
            try
            {
                row.push_back(stod(value)); // Convertir a double
            }
            catch (const invalid_argument &e)
            {
                cerr << "Error en conversión de datos: " << value << endl;
                row.push_back(0.0);
            }
        }
        data.push_back(row);
    }

    return data;
}

void readFromRedisDBEncapsulation(vector<vector<double>> &pressures_left, vector<vector<double>> &pressures_right)
{
    pressures_left = getCSVFromRedis("r");
    pressures_right = getCSVFromRedis("l");
}

void consumeFromQueue(const std::string &queue)
{
    redisContext *context = redisConnect("redis_container", 6379);
    if (context == nullptr || context->err)
    {
        std::cerr << "❌ Error al conectar con Redis: " << (context ? context->errstr : "Desconocido") << std::endl;
        return;
    }

    std::string lastID = "0"; // Comenzar desde el inicio

    while (true)
    {
        cout << "🔍 Buscando mensajes en la cola..." << std::endl;
        // Espera indefinida hasta recibir un mensaje (Espera Pasiva)
        redisReply *reply = (redisReply *)redisCommand(context, "XREAD BLOCK 0 STREAMS %s %s", queue.c_str(), lastID.c_str());

        if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements > 0)
        {
            for (size_t i = 0; i < reply->elements; i++)
            {
                redisReply *stream = reply->element[i];
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

                            std::string lado1, datos1, lado2, datos2;

                            if (msgID->type == REDIS_REPLY_STRING)
                            {
                                lastID = msgID->str; // Guardamos el último ID leído
                                std::cout << "📩 Mensaje recibido (" << lastID << "): ";

                                for (size_t k = 0; k < msgData->elements; k += 2)
                                {
                                    redisReply *field = msgData->element[k];
                                    redisReply *value = msgData->element[k + 1];

                                    if (field->type == REDIS_REPLY_STRING && value->type == REDIS_REPLY_STRING)
                                    {
                                        if (std::string(field->str) == "id1")
                                            lado1 = value->str;
                                        if (std::string(field->str) == "id2")
                                            datos1 = value->str;
                                        if (std::string(field->str) == "id3")
                                            lado2 = value->str;
                                    }
                                }

                                // Imprimir el par de valores
                                std::cout << "[(" << lado1 << ", " << datos1 << "), (" << lado2 << ", " << datos2 << ")]" << std::endl;
                            }
                        }
                    }
                }
            }
        }

        if (reply) // Para liberar memoria, pero nunca se va a ejecutar debido al while true
        {
            freeReplyObject(reply);
        }
    }

    redisFree(context);
}

void consumeFromQueue(const std::string &queue)
{
    redisContext *context = redisConnect("redis_container", 6379);
    if (context == nullptr || context->err)
    {
        std::cerr << "❌ Error al conectar con Redis: " << (context ? context->errstr : "Desconocido") << std::endl;
        return;
    }

    std::string lastID = "0"; // Comenzar desde el inicio

    while (true)
    {
        cout << "🔍 Buscando mensajes en la cola..." << std::endl;
        // Espera indefinida hasta recibir un mensaje (Espera Pasiva)
        redisReply *reply = (redisReply *)redisCommand(context, "XREAD BLOCK 0 STREAMS %s %s", queue.c_str(), lastID.c_str());

        if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements > 0)
        {
            for (size_t i = 0; i < reply->elements; i++)
            {
                redisReply *stream = reply->element[i];
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

                            int wearableId_L = -1, wearableId_R = -1, experimentId = -1, participantId = -1, sWId = -1, trialId = -1;

                            if (msgID->type == REDIS_REPLY_STRING)
                            {
                                lastID = msgID->str; // Guardamos el último ID leído
                                std::cout << "📩 Mensaje recibido (" << lastID << "): ";

                                for (size_t k = 0; k < msgData->elements; k += 2)
                                {
                                    redisReply *field = msgData->element[k];
                                    redisReply *value = msgData->element[k + 1];

                                    if (field->type == REDIS_REPLY_STRING && value->type == REDIS_REPLY_STRING)
                                    {
                                        if (std::string(field->str) == "wearableId_L")
                                            wearableId_L = std::stoi(value->str);
                                        else if (std::string(field->str) == "wearableId_R")
                                            wearableId_R = std::stoi(value->str);
                                        else if (std::string(field->str) == "experimentId")
                                            experimentId = std::stoi(value->str);
                                        else if (std::string(field->str) == "participantId")
                                            participantId = std::stoi(value->str);
                                        else if (std::string(field->str) == "sWId")
                                            sWId = std::stoi(value->str);
                                        else if (std::string(field->str) == "trialId")
                                            trialId = std::stoi(value->str);
                                    }
                                }

                                // Imprimir el par de valores
                                std::cout << "wearableId_L: " << wearableId_L << ", wearableId_R: " << wearableId_R
                                          << ", experimentId: " << experimentId << ", participantId: " << participantId
                                          << ", sWId: " << sWId << ", trialId: " << trialId << std::endl;
                            }
                        }
                    }
                }
            }
        }

        if (reply) // Para liberar memoria, pero nunca se va a ejecutar debido al while true
        {
            freeReplyObject(reply);
        }
    }

    redisFree(context);
}
