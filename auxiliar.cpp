#include <iostream>
#include <libpq-fe.h>

using namespace std;

PGconn *cnn = NULL;
PGresult *result = NULL;

const char *host = "localhost";
const char *port = "5432";
const char *dataBase = "ssith-db";
const char *user = "admin";
const char *passwd = "admin";

int main()
{
    // Conectarse a PostgreSQL
    cnn = PQsetdbLogin(host, port, NULL, NULL, dataBase, user, passwd);

    if (cnn == NULL || PQstatus(cnn) == CONNECTION_BAD)
    {
        cerr << "Error de conexión a PostgreSQL: " << (cnn ? PQerrorMessage(cnn) : "No se pudo establecer la conexión") << endl;
        PQfinish(cnn);
        return 1;
    }

    cout << "Conectado a PostgreSQL!" << endl;

    // 🔹 1. Insertar datos en la base de datos
    const char *insertQuery = "UPDATE trial SET \"heatMapVideoPath\"='urldeejemplo' WHERE id=34;";

    result = PQexec(cnn, insertQuery);

    if (!result || PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        cerr << "Error al actualizar datos: " << (result ? PQerrorMessage(cnn) : "Error en la consulta SQL") << endl;
        if (result)
            PQclear(result);
        PQfinish(cnn);
        return 1;
    }

    cout << "Datos actualizados correctamente." << endl;
    PQclear(result); // Liberar memoria después de la inserción

    PQfinish(cnn);

    return 0;
}
