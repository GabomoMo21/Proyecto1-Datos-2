#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <random>
#include <limits>

using namespace std;
using namespace std::chrono;

bool guardarResultadoCSV(const string& tamano,
                         const string& input,
                         const string& modo,
                         long long tiempo) {
    string nombreCSV = "resultadosgenerator.csv";

    bool escribirEncabezado = false;

    if (!filesystem::exists(nombreCSV) || filesystem::is_empty(nombreCSV)) {
        escribirEncabezado = true;
    }

    ofstream archivoCSV(nombreCSV, ios::app);
    if (!archivoCSV.is_open()) {
        cerr << "Error al abrir el archivo CSV" << endl;
        return false;
    }

    if (escribirEncabezado) {
        archivoCSV << "Tamano,Input,Modo,Tiempo\n";
    }

    archivoCSV << tamano << ","
               << input << ","
               << modo << ","
               << tiempo << "\n";

    archivoCSV.close();
    return true;
}

bool validarArgumentos(int argc,
                       char* argv[],
                       string& opcion,
                       string& archivo,
                       string& modo) {
    if (argc != 7) {
        cerr << "Uso incorrecto. Ejemplo: ./generator -size SMALL -output datos.bin -mode SMALL_RANGE" << endl;
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

    if (string(argv[5]) != "-mode") {
        cerr << "Falta -mode" << endl;
        return false;
    }

    opcion = argv[2];
    archivo = argv[4];
    modo = argv[6];

    if (opcion != "SMALL" && opcion != "MEDIUM" && opcion != "LARGE") {
        cerr << "Tamano no reconocido. Use SMALL, MEDIUM o LARGE" << endl;
        return false;
    }

    if (modo != "SMALL_RANGE" && modo != "FULL_RANGE") {
        cerr << "Modo no reconocido. Use SMALL_RANGE o FULL_RANGE" << endl;
        return false;
    }

    return true;
}

long long obtenerCantidadEnteros(const string& opcion) {
    if (opcion == "SMALL") {
        return (256LL * 1024 * 1024) / 4;
    }
    else if (opcion == "MEDIUM") {
        return (512LL * 1024 * 1024) / 4;
    }
    else if (opcion == "LARGE"){
        return (1024LL * 1024 * 1024) / 4;
    }
    else {
        return -1;
    }
}

int main(int argc, char* argv[]) {
    string opcion;
    string archivo;
    string modo;

    if (!validarArgumentos(argc, argv, opcion, archivo, modo)) {
        return 1;
    }

    auto inicio = high_resolution_clock::now();

    ofstream archivo2(archivo, ios::out | ios::binary | ios::trunc);
    if (!archivo2.is_open()) {
        cerr << "Error al abrir el archivo binario." << endl;
        return 1;
    }

    long long cantidadEnteros = obtenerCantidadEnteros(opcion);

    if (opcion == "SMALL") {
        cout << "pequeno" << endl;
    }
    else if (opcion == "MEDIUM") {
        cout << "mediano" << endl;
    }
    else {
        cout << "grande" << endl;
    }

    random_device rd;
    mt19937 generador(rd());

    uniform_int_distribution<int> distPequena(1000, 9999);
    uniform_int_distribution<int> distCompleta(numeric_limits<int>::min(),
                                               numeric_limits<int>::max());

    for (long long i = 0; i < cantidadEnteros; i++) {
        int x;

        if (modo == "SMALL_RANGE") {
            x = distPequena(generador);
        } else {
            x = distCompleta(generador);
        }

        archivo2.write(reinterpret_cast<char*>(&x), sizeof(x));

        if (!archivo2) {
            cerr << "Error al escribir en el archivo binario." << endl;
            archivo2.close();
            return 1;
        }
    }

    archivo2.close();

    auto fin = high_resolution_clock::now();
    auto duracion = duration_cast<microseconds>(fin - inicio);

    cout << duracion.count() << endl;

    bool csvGuardado = guardarResultadoCSV(opcion, archivo, modo, duracion.count());
    if (!csvGuardado) {
        cerr << "No se pudo guardar el resultado en el CSV." << endl;
        return 1;
    }

    return 0;
}