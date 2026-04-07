#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>
#include <cstdlib>

using namespace std;
using namespace std::chrono;

bool guardarResultadoCSV(const string& tamano,
                         const string& input,
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
        archivoCSV << "Tamano,Input,Tiempo\n";
    }

    archivoCSV << tamano << ","
               << input << ","
               << tiempo << "\n";

    archivoCSV.close();
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        cerr << "Uso incorrecto. Ejemplo: ./programa -size small -file datos.bin" << endl;
        return 1;
    }

    auto inicio = high_resolution_clock::now();

    string opcion = argv[2];
    string archivo = argv[4];

    ofstream archivo2(archivo, ios::out | ios::binary | ios::trunc);

    if (!archivo2.is_open()) {
        cerr << "Error al abrir el archivo binario." << endl;
        return 1;
    }

    if (opcion == "SMALL") {
        cout << "pequeno" << endl;
        for (int i = 0; i < (32 * 1024 * 1024)/4; i++) {
            int x = rand() % 9000 + 1000;
            archivo2.write((char*)&x, sizeof(x));
        }
    }
    else if (opcion == "MEDIUM") {
        cout << "mediano" << endl;
        for (int i = 0; i < (512 * 1024 * 1024)/4; i++) {
            int x = rand() % 9000 + 1000;
            archivo2.write((char*)&x, sizeof(x));
        }
    }
    else {
        cout << "LARGE" << endl;
        for (int i = 0; i < (1024 * 1024 * 1024)/4; i++) {
            int x = rand() % 9000 + 1000;
            archivo2.write((char*)&x, sizeof(x));
        }
    }

    archivo2.close();

    auto fin = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(fin - inicio);

    cout << duration.count() << endl;

    bool csvGuardado = guardarResultadoCSV(opcion, archivo, duration.count());
    if (!csvGuardado) {
        cerr << "No se pudo guardar el resultado en el CSV." << endl;
        return 1;
    }

    return 0;
}