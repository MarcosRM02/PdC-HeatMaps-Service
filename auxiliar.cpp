// ------------------------------------------------------------------

// Función para conectarse a Redis y obtener los datos CSV
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

-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -void consumeFromQueue(const std::string &queue)
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

// Función del hilo que escribe los frames a ffmpeg con parámetros para reducir la calidad
// void writer_thread_function(const string &filename, int width, int height, double fps)
// {
//     // Se ajustan parámetros para menor calidad:
//     // - CRF: valor mayor implica mayor compresión (menor calidad); aquí usamos 28.
//     // - Bitrate: se establece un bitrate de 500 kbps.
//     string cmd = "ffmpeg -y -threads 8 -f rawvideo -pixel_format bgr24 -video_size " +
//                  to_string(width) + "x" + to_string(height) +
//                  " -framerate " + to_string(fps) +
//                  " -i pipe:0 -c:v libx264 -preset fast -crf 28 -b:v 500k -pix_fmt yuv420p \"" + filename + "\"";

//     FILE *ffmpeg_pipe = popen(cmd.c_str(), "w");
//     if (!ffmpeg_pipe)
//     {
//         cerr << "Error: No se pudo iniciar ffmpeg." << endl;
//         return;
//     }

//     while (true)
//     {
//         Mat frame;
//         {
//             unique_lock<mutex> lock(queue_mutex);
//             queue_cond.wait(lock, []
//                             { return !frame_queue.empty() || processing_done; });
//             if (processing_done && frame_queue.empty())
//                 break;

//             if (!frame_queue.empty())
//             {
//                 frame = frame_queue.front();
//                 frame_queue.pop();
//             }
//         }

//         if (!frame.empty())
//         {
//             size_t bytes_written = fwrite(frame.data, 1, frame.total() * frame.elemSize(), ffmpeg_pipe);
//             if (bytes_written != frame.total() * frame.elemSize())
//             {
//                 cerr << "Error al escribir el frame." << endl;
//             }
//         }
//     }

//     pclose(ffmpeg_pipe);
// }

// // Función para generar la animación combinada usando ffmpeg para escribir el video
// void generate_combined_animation(
//     const vector<Mat> &frames_left,
//     const vector<Mat> &frames_right,
//     const string &filename,
//     vector<pair<double, double>> &coordinates_left,
//     vector<pair<double, double>> &coordinates_right,
//     double fps = 50.0)
// {
//     if (frames_left.empty() || frames_right.empty())
//     {
//         cerr << "No hay cuadros para guardar." << endl;
//         return;
//     }

//     // Mantener la resolución original (la misma que se utiliza para pintar los sensores)
//     int width = frames_left[0].cols + frames_right[0].cols;
//     int height = frames_left[0].rows;

//     // Iniciar el hilo escritor con ffmpeg y los parámetros de compresión deseados
//     processing_done = false;
//     thread writer_thread(writer_thread_function, filename, width, height, fps);

//     // Procesar cada frame, combinarlos, aplicar colormap y dibujar la superposición de puntos
//     for (size_t i = 0; i < frames_left.size(); ++i)
//     {
//         Mat combined_frame;
//         hconcat(frames_left[i], frames_right[i], combined_frame); // Concatenar horizontalmente

//         Mat combined_frame_8UC1;
//         combined_frame.convertTo(combined_frame_8UC1, CV_8UC1, 255.0 / 4095.0); // Escalar a 8 bits

//         Mat color_frame;
//         applyColorMap(combined_frame_8UC1, color_frame, COLORMAP_JET); // Aplicar el colormap

//         // Dibujar la superposición de puntos sobre la parte izquierda
//         visualize_heatmap(color_frame, coordinates_left);

//         // Para la parte derecha, ajustar las coordenadas sumándole el ancho de la imagen izquierda
//         vector<pair<double, double>> coordinates_right_offset;
//         for (const auto &pt : coordinates_right)
//         {
//             coordinates_right_offset.emplace_back(pt.first + frames_left[0].cols, pt.second);
//         }
//         visualize_heatmap(color_frame, coordinates_right_offset);

//         // Agregar el frame a la cola de forma segura
//         {
//             lock_guard<mutex> lock(queue_mutex);
//             frame_queue.push(color_frame.clone());
//         }
//         queue_cond.notify_one();
//     }

//     // Señalizar fin del procesamiento
//     {
//         lock_guard<mutex> lock(queue_mutex);
//         processing_done = true;
//     }
//     queue_cond.notify_one();

//     // Esperar a que el hilo escritor termine
//     writer_thread.join();
//     cout << "Animación combinada guardada como " << filename << endl;
// }

// void generate_combined_animation_sequential(
//     const vector<Mat> &frames_left,
//     const vector<Mat> &frames_right,
//     const string &filename,
//     vector<pair<double, double>> &coordinates_left,
//     vector<pair<double, double>> &coordinates_right,
//     double fps = 50.0)
// {
//     if (frames_left.empty() || frames_right.empty())
//     {
//         cerr << "No hay cuadros para guardar." << endl;
//         return;
//     }

//     // Resolución combinada (se mantiene igual para que se vean los sensores)
//     int width = frames_left[0].cols + frames_right[0].cols;
//     int height = frames_left[0].rows;

//     // Construir el comando para FFmpeg (ajustando parámetros para reducir calidad)
//     string cmd = "ffmpeg -y -threads 1 -f rawvideo -pixel_format bgr24 -video_size " +
//                  to_string(width) + "x" + to_string(height) +
//                  " -framerate " + to_string(fps) +
//                  " -i pipe:0 -c:v libx264 -preset fast -crf 28 -b:v 500k -pix_fmt yuv420p \"" + filename + "\"";

//     // Abrir el pipe hacia FFmpeg
//     FILE *ffmpeg_pipe = popen(cmd.c_str(), "w");
//     if (!ffmpeg_pipe)
//     {
//         cerr << "Error: No se pudo iniciar FFmpeg." << endl;
//         return;
//     }

//     // Procesar cada frame secuencialmente para mantener el orden
//     for (size_t i = 0; i < frames_left.size(); ++i)
//     {
//         // Combinar horizontalmente el frame izquierdo y derecho
//         Mat combined_frame;
//         hconcat(frames_left[i], frames_right[i], combined_frame);

//         // Convertir a 8 bits (necesario para aplicar el colormap)
//         Mat combined_frame_8UC1;
//         combined_frame.convertTo(combined_frame_8UC1, CV_8UC1, 255.0 / 4095.0);

//         // Aplicar el colormap
//         Mat color_frame;
//         applyColorMap(combined_frame_8UC1, color_frame, COLORMAP_JET);

//         // Dibujar los sensores en la parte izquierda
//         visualize_heatmap(color_frame, coordinates_left);

//         // Ajustar las coordenadas para la parte derecha sumando el ancho del frame izquierdo
//         vector<pair<double, double>> coordinates_right_offset;
//         for (const auto &pt : coordinates_right)
//         {
//             coordinates_right_offset.emplace_back(pt.first + frames_left[0].cols, pt.second);
//         }
//         visualize_heatmap(color_frame, coordinates_right_offset);

//         // Escribir el frame directamente en el pipe de FFmpeg
//         size_t bytes_written = fwrite(color_frame.data, 1, color_frame.total() * color_frame.elemSize(), ffmpeg_pipe);
//         if (bytes_written != color_frame.total() * color_frame.elemSize())
//         {
//             cerr << "Error al escribir el frame " << i << endl;
//         }
//     }

//     // Cerrar el pipe para finalizar la escritura del video
//     pclose(ffmpeg_pipe);
//     cout << "Animación combinada guardada como " << filename << endl;
// }

// void generate_combined_animation(
//     const vector<Mat> &frames_left,
//     const vector<Mat> &frames_right,
//     const string &filename, vector<pair<double, double>> &coordinates_left, vector<pair<double, double>> &coordinates_right, double fps = 50.0)
// {
//     if (frames_left.empty() || frames_right.empty())
//     {
//         cerr << "No hay cuadros para guardar." << endl;
//         return;
//     }

//     int width = frames_left[0].cols + frames_right[0].cols; // Ancho combinado de los dos mapas
//     int height = frames_left[0].rows;                       // La altura es la misma para ambos

//     VideoWriter writer(filename,
//                        VideoWriter::fourcc('H', '2', '6', '4'),
//                        fps,
//                        Size(width, height));

//     // Escribe cada cuadro (sin superposición) en el video
//     for (size_t i = 0; i < frames_left.size(); ++i)
//     {
//         // Combinar los dos mapas de calor en una sola imagen
//         Mat combined_frame;
//         hconcat(frames_left[i], frames_right[i], combined_frame); // Concatenar horizontalmente

//         Mat combined_frame_8UC1;
//         combined_frame.convertTo(combined_frame_8UC1, CV_8UC1, 255.0 / 4095.0); // Escalar a 8 bits

//         Mat color_frame;
//         applyColorMap(combined_frame_8UC1, color_frame, COLORMAP_JET); // Aplicar el colormap

//         // Aquí se dibujarán los puntos y números sobre la imagen ya coloreada.
//         // Se asume que 'coordinates_left' y 'coordinates_right' son variables globales
//         // o accesibles en este contexto.
//         visualize_heatmap(color_frame, coordinates_left); // Dibuja sobre la parte izquierda

//         // Calcular el offset para las coordenadas de la parte derecha
//         vector<pair<double, double>> coordinates_right_offset;
//         for (const auto &pt : coordinates_right)
//         {
//             // Se suma el ancho de la imagen izquierda (frames_left[0].cols)
//             coordinates_right_offset.emplace_back(pt.first + frames_left[0].cols, pt.second);
//         }
//         visualize_heatmap(color_frame, coordinates_right_offset); // Dibuja sobre la parte derecha

//         writer.write(color_frame); // Escribir el cuadro en el archivo de video
//     }

// Ahora, preparar el cuadro de superposición (se hará solo una vez)
// Generamos un cuadro final combinando el último frame de cada lado

//     writer.release();
//     cout << "Animación combinada guardada como " << filename << endl;
// }

// Función del hilo que escribe los frames en ffmpeg
// void writer_thread_function(const string &filename, int width, int height, double fps)
// {
//     // Construir el comando de ffmpeg
//     string cmd = "ffmpeg -y -threads 8 -f rawvideo -pixel_format bgr24 -video_size " +
//                  to_string(width) + "x" + to_string(height) +
//                  " -framerate " + to_string(fps) +
//                  " -i pipe:0 -c:v libx264 -preset fast -crf 23 -x264-params \"threads=8\" " +
//                  "-pix_fmt yuv420p \"" + filename + "\"";

//     // Abrir el pipe a ffmpeg
//     FILE *ffmpeg_pipe = popen(cmd.c_str(), "w");

//     if (!ffmpeg_pipe)
//     {
//         cerr << "Error: No se pudo iniciar ffmpeg." << endl;
//         return;
//     }

//     // Leer frames de la cola y escribirlos

//     while (true)
//     {

//         Mat frame;

//         {
//             unique_lock<mutex> lock(queue_mutex);
//             queue_cond.wait(lock, []
//                             { return !frame_queue.empty() || processing_done; });

//             if (processing_done && frame_queue.empty())
//                 break;

//             if (!frame_queue.empty())
//             {
//                 frame = frame_queue.front();
//                 frame_queue.pop();
//             }
//         }

//         if (!frame.empty())
//         {
//             // Escribir el frame en formato rawvideo
//             size_t bytes_written = fwrite(frame.data, 1, frame.total() * frame.elemSize(), ffmpeg_pipe);
//             if (bytes_written != frame.total() * frame.elemSize())
//             {

//                 cerr << "Error al escribir el frame." << endl;
//             }
//         }
//     }

//     // Cerrar el pipe
//     pclose(ffmpeg_pipe);
// }

// void generate_combined_animation(const vector<Mat> &frames_left, const vector<Mat> &frames_right,
//                                  const string &filename, vector<pair<double, double>> &coordinates_left, vector<pair<double, double>> &coordinates_right, double fps = 50.0)
// {
//     if (frames_left.empty() || frames_right.empty())
//     {
//         cerr << "No hay cuadros para guardar." << endl;
//         return;
//     }

//     // int width = frames_left[0].cols + frames_right[0].cols;
//     // int height = frames_left[0].rows;

//     int width = 175;
//     int height = 520;

//     // Iniciar hilo escritor
//     processing_done = false;
//     thread writer_thread(writer_thread_function, filename, width, height, fps);

//     // Procesar cada frame y añadirlo a la cola
//     for (size_t i = 0; i < frames_left.size(); ++i)
//     {
//         Mat combined_frame;
//         hconcat(frames_left[i], frames_right[i], combined_frame);

//         Mat combined_frame_8UC1;
//         combined_frame.convertTo(combined_frame_8UC1, CV_8UC1, 255.0 / 4095.0);
//         Mat color_frame;
//         applyColorMap(combined_frame_8UC1, color_frame, COLORMAP_JET);

//         // Aquí se dibujan los círculos y textos sobre la imagen ya coloreada.
//         // Para la parte izquierda:
//         // for (size_t j = 0; j < frames_left[i].cols; ++j)
//         // {
//         // } // solo para ejemplificar, lo correcto es usar tus coordenadas
//         // Supongamos que tienes las coordenadas para el lado izquierdo y derecho:
// visualize_heatmap(color_frame, coordinates_left); // Dibuja sobre la parte izquierda
// // Para la parte derecha, hay que ajustar las coordenadas sumándole el ancho de la imagen izquierda.
// vector<pair<double, double>> coordinates_right_offset;
// for (const auto &pt : coordinates_right)
// {
//     coordinates_right_offset.emplace_back(pt.first + frames_left[i].cols, pt.second);
// }
// visualize_heatmap(color_frame, coordinates_right_offset);

//         // Añadir frame a la cola de forma segura
//         {
//             lock_guard<mutex> lock(queue_mutex);
//             frame_queue.push(color_frame.clone()); // Clonar para evitar aliasing
//         }

//         queue_cond.notify_one();
//     }

//     // Señalizar fin del procesamiento
//     {
//         lock_guard<mutex> lock(queue_mutex);
//         processing_done = true;
//     }
//     queue_cond.notify_one();

//     // Esperar a que el hilo escritor termine
//     writer_thread.join();
//     cout << "Animación combinada guardada como " << filename << endl;
// }