#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
#include <cstdlib>
#include <random>
#include <limits>

using namespace std;
using namespace std::chrono;

// cambiar para el rango acotado
const bool USAR_RANGO_ACOTADO = false;

//verificar la entrada de argumentos
bool validarArgumentos(int argc,
                       char* argv[],
                       string& opcion,
                       string& archivo) {
    if (argc != 5) {
        cerr << "Uso incorrecto. Ejemplo: ./generator -size SMALL -output datos.bin" << endl;
        return false;
    }

    if (string(argv[1]) != "-size") {
        cerr << "Falta -size" << endl;
        return false;
    }

    if (string(argv[3]) != "-output") {
        cerr << "Falta -output" << endl;
        return false;
    }

    opcion = argv[2];
    archivo = argv[4];

    if (opcion != "SMALL" && opcion != "MEDIUM" && opcion != "LARGE") {
        cerr << "Tamano no reconocido. Use SMALL, MEDIUM o LARGE" << endl;
        return false;
    }

    return true;
}
//calcula la cantidad de enteros
long long obtenerCantidadEnteros(const string& opcion) {
    if (opcion == "SMALL") {
        return (256LL * 1024 * 1024) / 4;
    }
    else if (opcion == "MEDIUM") {
        return (512LL * 1024 * 1024) / 4;
    }
    else if (opcion == "LARGE") {
        return (1024LL * 1024 * 1024) / 4;
    }
    else {
        return -1;
    }
}

int main(int argc, char* argv[]) {
    string opcion;
    string archivo;

    //validacion de argumentos
    if (!validarArgumentos(argc, argv, opcion, archivo)) {
        return 1;
    }

    auto inicio = high_resolution_clock::now(); //inicia a correr tiempo

    //abre el archivo de salida
    ofstream archivoSalida(archivo, ios::out | ios::binary | ios::trunc);
    if (!archivoSalida.is_open()) {
        cerr << "Error al abrir el archivo binario." << endl;
        return 1;
    }
    //calcula enteros
    long long cantidadEnteros = obtenerCantidadEnteros(opcion);

    //imprime el tamano elegido
    if (opcion == "SMALL") {
        cout << "pequeno" << endl;
    }
    else if (opcion == "MEDIUM") {
        cout << "mediano" << endl;
    }
    else {
        cout << "grande" << endl;
    }

    //generador aleatorio
    random_device rd;
    mt19937 generador(rd());

    // rango acotado para counting
    uniform_int_distribution<int> distAcotada(1000, 9999);

    // rango completo
    uniform_int_distribution<int> distCompleta(numeric_limits<int>::min(),
                                               numeric_limits<int>::max());


    //escribe los numeros en el archivo
    for (long long i = 0; i < cantidadEnteros; i++) {
        int x;

        if (USAR_RANGO_ACOTADO) {
            x = distAcotada(generador);

        } else {
            x = distCompleta(generador);
        }

        archivoSalida.write(reinterpret_cast<char*>(&x), sizeof(x)); //escribir los bytes del entero

        if (!archivoSalida) {
            cerr << "Error al escribir en el archivo binario." << endl;
            archivoSalida.close();
            return 1;
        }
    }

    archivoSalida.close(); ///cerrar archivo

    //calcula el tiempo final
    auto fin = high_resolution_clock::now();
    auto duracion = duration_cast<microseconds>(fin - inicio);

    cout << duracion.count()/1000<< "ms" << endl; //impresion del tiempo durado

    return 0;
}