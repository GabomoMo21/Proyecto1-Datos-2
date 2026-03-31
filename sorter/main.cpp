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
    fstream archivo;
    vector<vector<int>> paginas;
    vector<int> pagenumbers;
    int totalElementos;
    int hits = 0;
    int faults = 0;
    int siguiente = 0;

    PagedArray(const string& filename, int pageSize, int pageCount)
        : intsPerPage(pageSize), pageCount(pageCount) {

        archivo.open(filename, ios::in | ios::out | ios::binary);
        if (!archivo) {
            cerr << "Error al abrir el archivo " << filename << endl;
            return;
        }

        archivo.seekg(0, ios::end);
        auto tamanoArchivo = archivo.tellg();
        totalElementos = tamanoArchivo / sizeof(int);
        archivo.seekg(0, ios::beg);

        paginas.resize(pageCount);
        for (int i = 0; i < pageCount; i++) {
            paginas[i].resize(intsPerPage);
        }

        pagenumbers.resize(pageCount, -1);
    }

    void cargarPagina(int pageNumber, int slot) {
        int pageStartByte = pageNumber * intsPerPage * sizeof(int);

        archivo.seekg(pageStartByte, ios::beg);

        for (int i = 0; i < intsPerPage; i++) {
            int value;
            archivo.read((char*)&value, sizeof(int));

            if (archivo) {
                paginas[slot][i] = value;
            } else {
                paginas[slot][i] = 0;
            }
        }

        archivo.clear();
        pagenumbers[slot] = pageNumber;
    }

    int get(int indice) {
        if (indice < 0 || indice >= totalElementos) {
            cerr << "Indice fuera de rango: " << indice << endl;
            return -1;
        }

        int pageNumber = indice / intsPerPage;
        int offset = indice % intsPerPage;

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

        int prov = siguiente;
        cargarPagina(pageNumber, prov);
        siguiente = (siguiente + 1) % pageCount;
        return paginas[prov][offset];
    }

    void set(int indice, int valor) {
        if (indice < 0 || indice >= totalElementos) {
            cerr << "Indice fuera de rango: " << indice << endl;
            return;
        }

        int pageNumber = indice / intsPerPage;
        int offset = indice % intsPerPage;

        for (int i = 0; i < pageCount; i++) {
            if (pageNumber == pagenumbers[i]) {
                hits++;
                paginas[i][offset] = valor;

                int posicionReal = indice * sizeof(int);
                archivo.seekp(posicionReal, ios::beg);
                archivo.write((char*)&valor, sizeof(int));
                archivo.flush();
                return;
            }
        }
        faults++;
        for (int j = 0; j < pageCount; j++) {
            if (pagenumbers[j] == -1) {
                cargarPagina(pageNumber, j);
                paginas[j][offset] = valor;

                int posicionReal = indice * sizeof(int);
                archivo.seekp(posicionReal, ios::beg);
                archivo.write((char*)&valor, sizeof(int));
                archivo.flush();
                return;
            }
        }

        int prov = siguiente;
        cargarPagina(pageNumber, prov);
        paginas[prov][offset] = valor;

        int posicionReal = indice * sizeof(int);
        archivo.seekp(posicionReal, ios::beg);
        archivo.write((char*)&valor, sizeof(int));
        archivo.flush();

        siguiente = (siguiente + 1) % pageCount;
    }

    int size() {
        return totalElementos;
    }
};

void bubblesort(PagedArray& array, int n) {
    bool sorted;

    for (int i = 0; i < n - 1; i++) {
        sorted = false;

        for (int j = 0; j < n - i - 1; j++) {
            int a = array.get(j);
            int b = array.get(j + 1);

            if (a > b) {
                array.set(j, b);
                array.set(j + 1, a);
                sorted = true;
            }
        }

        if (!sorted) {
            break;
        }
    }
}


void cambiar(PagedArray& arreglo, int a, int b) {
    int temp = arreglo.get(a);
    int valorB = arreglo.get(b);

    arreglo.set(a, valorB);
    arreglo.set(b, temp);
}

int partition(PagedArray& arreglo, int low, int high) {
    int pivot = arreglo.get(high);

    // Index of elemment just before the last element
    // It is used for swapping
    int i = (low - 1);

    for (int j = low; j <= high - 1; j++) {
        int actual = arreglo.get(j);
        // If current element is smaller than or
        // equal to pivot
        if (actual <= pivot) {
            i++;
            cambiar(arreglo, i, j);
        }
    }

    // Put pivot to its position
    cambiar(arreglo,i+1, high);

    // Return the point of partition
    return (i + 1);

}

void quicksort(PagedArray& arreglo, int low, int high) {
    if (low < high) {

        // pi is Partitioning Index, arr[p] is now at
        // right place
        int pi = partition(arreglo, low, high);

        // Separately sort elements before and after the
        // Partition Index pi
        quicksort(arreglo, low, pi - 1);
        quicksort(arreglo, pi + 1, high);
    }
}

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

    ifstream archivo_entrada(input, ios::binary);
    if (!archivo_entrada.is_open()) {
        cout << "Error al abrir el archivo_entrada" << endl;
        return 1;
    }
    ofstream archivo_salida(output, ios::binary);
    if (!archivo_salida.is_open()) {
        cout << "Error al abrir el archivo_salida" << endl;
        return 1;
    }
    archivo_salida<<archivo_entrada.rdbuf();
    archivo_entrada.close();
    archivo_salida.close();

    PagedArray arreglo(output, pageSize, pageCount);
    int n = min(arreglo.size(), 10000);
    if (algoritmo == "bubble") {
        bubblesort(arreglo, n);
    }

    if (algoritmo == "quick") {
        quicksort(arreglo, 0 , n-1);
    }




    cout << "Total de elementos: " << arreglo.totalElementos << endl;
    cout << arreglo.get(0) << endl;
    cout << arreglo.get(1) << endl;
    cout << arreglo.get(2) << endl;
    cout << "Hits: " << arreglo.hits << endl;
    cout << "Faults: " << arreglo.faults << endl;
    return 0;
}