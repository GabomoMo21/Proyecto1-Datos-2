#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

using namespace std;

class PagedArray {
public:
    int intsPerPage;
    int pageCount;
    ifstream lectura;
    vector<vector<int>> paginas;
    vector<int> pagenumbers;
    int totalElementos;
    int hits = 0;
    int faults = 0;
    int siguiente = 0;

    PagedArray(const string& filename, int pageSize, int pageCount)
        : intsPerPage(pageSize), pageCount(pageCount) {

        lectura.open(filename, ios::binary);
        if (!lectura) {
            cerr << "Error al abrir el archivo " << filename << endl;
        }

        lectura.seekg(0, ios::end);
        auto tamanoArchivo = lectura.tellg();
        totalElementos = tamanoArchivo / sizeof(int);
        lectura.seekg(0, ios::beg);
        paginas.resize(pageCount);
        for (int i = 0; i < pageCount; i++) {
            paginas[i].resize(intsPerPage);
        }
        pagenumbers.resize(pageCount, -1);
    }

    void cargarPagina(int pageNumber, int slot) {
        int pageStartByte = pageNumber * intsPerPage * sizeof(int);

        lectura.seekg(pageStartByte, ios::beg);

        for (int i = 0; i < intsPerPage; i++) {
            int value;
            lectura.read((char*)&value, sizeof(int));
            paginas[slot][i] = value;
        }
        pagenumbers[slot] = pageNumber;
    }

    int get(int indice) {
        if (indice < 0 || indice >= totalElementos) {
            cerr << "Indice fuera de rango: " << indice << endl;
            return -1;
        }

        int pageNumber = indice / intsPerPage;
        int offset = indice % intsPerPage;
        int prov;

        for (int i = 0; i < pageCount; i++) {
            if (pageNumber == pagenumbers[i]) {
                hits++;
                return paginas[i][offset];
            }
        }

        faults++;
        for (int j = 0; j < pageCount; j++) {
            if (pagenumbers[j] == -1) {
                cargarPagina(pageNumber, j);
                return paginas[j][offset];
            }
        }
        prov = siguiente;
        cargarPagina(pageNumber, prov);
        siguiente = (siguiente + 1) % pageCount;
        return paginas[prov][offset];
    }
};

int main(int argc, char* argv[]) {
    if (argc < 11) {
        cerr << "Uso: sorter -input <archivo_entrada> -output <archivo_salida> -alg <algoritmo> -pageSize <page_size> -pageCount <page_count>" << endl;
        return 1;
    }

    string input = argv[2];
    string output = argv[4];
    string algoritmo = argv[6];
    int pageSize = atoi(argv[8]);
    int pageCount = atoi(argv[10]);

    PagedArray arreglo(input, pageSize, pageCount);

    cout << "Total de elementos: " << arreglo.totalElementos << endl;
    cout << arreglo.get(0) << endl;
    cout << arreglo.get(1024) << endl;
    cout << arreglo.get(2048) << endl;
    cout << "Hits: " << arreglo.hits << endl;
    cout << "Faults: " << arreglo.faults << endl;
    return 0;
}