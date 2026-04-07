#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <filesystem>
using namespace std;
using namespace std::chrono;

class PagedArray {
public:
    class Referencia {
    private:
        PagedArray& arreglo;
        int indice;

    public:
        Referencia(PagedArray& arr, int idx) : arreglo(arr), indice(idx) {}

        Referencia& operator=(int valor) {
            arreglo.set(indice, valor);
            return *this;
        }

        Referencia& operator=(const Referencia& otra) {
            int valor = (int)otra;
            arreglo.set(indice, valor);
            return *this;
        }

        operator int() const {
            return arreglo.get(indice);
        }
    };

private:
    int intsPerPage;
    int pageCount;
    fstream archivo;
    int** paginas;
    int* pageNumbers;
    bool* dirtyFlags;
    int totalElementos;
    long long hits;
    long long faults;
    int siguiente;

public:
    PagedArray(const string& filename, int pageSize, int cantidadPaginas)
        : intsPerPage(pageSize),
          pageCount(cantidadPaginas),
          paginas(nullptr),
          pageNumbers(nullptr),
          dirtyFlags(nullptr),
          totalElementos(0),
          hits(0),
          faults(0),
          siguiente(0) {

        archivo.open(filename, ios::in | ios::out | ios::binary);
        if (!archivo.is_open()) {
            cerr << "Error al abrir el archivo " << filename << endl;
            return;
        }

        archivo.seekg(0, ios::end);
        streampos tamanoArchivo = archivo.tellg();
        totalElementos = (int)(tamanoArchivo / (streampos)sizeof(int));
        archivo.seekg(0, ios::beg);

        paginas = new int*[pageCount];
        for (int i = 0; i < pageCount; i++) {
            paginas[i] = new int[intsPerPage];
            for (int j = 0; j < intsPerPage; j++) {
                paginas[i][j] = 0;
            }
        }

        pageNumbers = new int[pageCount];
        dirtyFlags = new bool[pageCount];

        for (int i = 0; i < pageCount; i++) {
            pageNumbers[i] = -1;
            dirtyFlags[i] = false;
        }
    }

    PagedArray(const PagedArray&) = delete;
    PagedArray& operator=(const PagedArray&) = delete;

    ~PagedArray() {
        flushTodasLasPaginas();

        if (paginas != nullptr) {
            for (int i = 0; i < pageCount; i++) {
                delete[] paginas[i];
            }
            delete[] paginas;
        }

        delete[] pageNumbers;
        delete[] dirtyFlags;

        if (archivo.is_open()) {
            archivo.close();
        }
    }

    Referencia operator[](int indice) {
        return Referencia(*this, indice);
    }

    long long getHits() const {
        return hits;
    }

    long long getFaults() const {
        return faults;
    }

    int getTotalElementos() const {
        return totalElementos;
    }

    int size() const {
        return totalElementos;
    }

    int get(int indice) {
        if (indice < 0 || indice >= totalElementos) {
            cerr << "Indice fuera de rango: " << indice << endl;
            return -1;
        }

        int pageNumber = indice / intsPerPage;
        int offset = indice % intsPerPage;

        int slot = asegurarPaginaCargada(pageNumber);
        return paginas[slot][offset];
    }

    void set(int indice, int valor) {
        if (indice < 0 || indice >= totalElementos) {
            cerr << "Indice fuera de rango: " << indice << endl;
            return;
        }

        int pageNumber = indice / intsPerPage;
        int offset = indice % intsPerPage;

        int slot = asegurarPaginaCargada(pageNumber);
        paginas[slot][offset] = valor;
        dirtyFlags[slot] = true;
    }

private:
    int buscarSlotPagina(int pageNumber) const {
        for (int i = 0; i < pageCount; i++) {
            if (pageNumbers[i] == pageNumber) {
                return i;
            }
        }
        return -1;
    }

    int buscarSlotLibre() const {
        for (int i = 0; i < pageCount; i++) {
            if (pageNumbers[i] == -1) {
                return i;
            }
        }
        return -1;
    }

    void escribirPaginaSiEsNecesario(int slot) {
        if (slot < 0 || slot >= pageCount) {
            return;
        }

        if (pageNumbers[slot] == -1 || !dirtyFlags[slot]) {
            return;
        }

        int pageNumber = pageNumbers[slot];
        int elementosAEscribir = elementosValidosEnPagina(pageNumber);
        int pageStartByte = pageNumber * intsPerPage * (int)sizeof(int);

        archivo.clear();
        archivo.seekp(pageStartByte, ios::beg);

        for (int i = 0; i < elementosAEscribir; i++) {
            archivo.write((char*)&paginas[slot][i], sizeof(int));
        }

        archivo.flush();
        dirtyFlags[slot] = false;
    }

    int elementosValidosEnPagina(int pageNumber) const {
        int inicio = pageNumber * intsPerPage;

        if (inicio >= totalElementos) {
            return 0;
        }

        int restantes = totalElementos - inicio;

        if (restantes >= intsPerPage) {
            return intsPerPage;
        }

        return restantes;
    }

    void cargarPagina(int pageNumber, int slot) {
        int pageStartByte = pageNumber * intsPerPage * (int)sizeof(int);
        int elementosALeer = elementosValidosEnPagina(pageNumber);

        archivo.clear();
        archivo.seekg(pageStartByte, ios::beg);

        for (int i = 0; i < elementosALeer; i++) {
            int value;
            archivo.read((char*)&value, sizeof(int));

            if (archivo) {
                paginas[slot][i] = value;
            } else {
                paginas[slot][i] = 0;
            }
        }

        for (int i = elementosALeer; i < intsPerPage; i++) {
            paginas[slot][i] = 0;
        }

        archivo.clear();
        pageNumbers[slot] = pageNumber;
        dirtyFlags[slot] = false;
    }

    int asegurarPaginaCargada(int pageNumber) {
        int slotExistente = buscarSlotPagina(pageNumber);
        if (slotExistente != -1) {
            hits++;
            return slotExistente;
        }

        faults++;

        int slotLibre = buscarSlotLibre();
        if (slotLibre != -1) {
            cargarPagina(pageNumber, slotLibre);
            return slotLibre;
        }

        int slotReemplazo = siguiente;
        escribirPaginaSiEsNecesario(slotReemplazo);
        cargarPagina(pageNumber, slotReemplazo);
        siguiente = (siguiente + 1) % pageCount;

        return slotReemplazo;
    }

    void flushTodasLasPaginas() {
        if (paginas == nullptr || pageNumbers == nullptr || dirtyFlags == nullptr) {
            return;
        }

        for (int i = 0; i < pageCount; i++) {
            escribirPaginaSiEsNecesario(i);
        }
    }
};

void intercambiar(PagedArray& arreglo, int a, int b) {
    int temporal = arreglo[a];
    arreglo[a] = arreglo[b];
    arreglo[b] = temporal;
}

//selectionsort
void selectionsort(PagedArray& arr, int n) {
    for (int i = 0; i < n - 1; i++) {
        int min_idx = i;

        for (int j = i + 1; j < n; j++) {
            if (arr[j] < arr[min_idx]) {
                min_idx = j;
            }
        }

        if (min_idx != i) {
            intercambiar(arr, i, min_idx);
        }
    }
}


// quicksort

int partition(PagedArray& arreglo, int low, int high) {
    int pivot = arreglo[high];
    int i = low - 1;

    for (int j = low; j <= high - 1; j++) {
        int actual = arreglo[j];
        if (actual <= pivot) {
            i++;
            intercambiar(arreglo, i, j);
        }
    }

    intercambiar(arreglo, i + 1, high);
    return i + 1;
}

void quicksort(PagedArray& arreglo, int low, int high) {
    if (low < high) {
        int pi = partition(arreglo, low, high);
        quicksort(arreglo, low, pi - 1);
        quicksort(arreglo, pi + 1, high);
    }
}

// mergesort
void merge(PagedArray& arreglo, int izq, int mid, int der) {
    int n1 = mid - izq + 1;
    int n2 = der - mid;

    int* izquierda = new int[n1];
    int* derecha = new int[n2];

    for (int i = 0; i < n1; i++) {
        izquierda[i] = arreglo[izq + i];
    }

    for (int i = 0; i < n2; i++) {
        derecha[i] = arreglo[mid + 1 + i];
    }

    int i = 0;
    int j = 0;
    int k = izq;

    while (i < n1 && j < n2) {
        if (izquierda[i] <= derecha[j]) {
            arreglo[k] = izquierda[i];
            i++;
        } else {
            arreglo[k] = derecha[j];
            j++;
        }
        k++;
    }

    while (i < n1) {
        arreglo[k] = izquierda[i];
        i++;
        k++;
    }

    while (j < n2) {
        arreglo[k] = derecha[j];
        j++;
        k++;
    }

    delete[] izquierda;
    delete[] derecha;
}

void mergesort(PagedArray& arreglo, int izq, int der) {
    if (izq >= der) {
        return;
    }

    int mid = izq + (der - izq) / 2;
    mergesort(arreglo, izq, mid);
    mergesort(arreglo, mid + 1, der);
    merge(arreglo, izq, mid, der);
}

// insertion sort
void insertionsort(PagedArray& arreglo, int n) {
    for (int i = 1; i < n; i++) {
        int clave = arreglo[i];
        int j = i - 1;

        while (j >= 0 && arreglo[j] > clave) {
            arreglo[j + 1] = arreglo[j];
            j--;
        }

        arreglo[j + 1] = clave;
    }
}

//heapsort
void heapify(PagedArray& arr, int n, int i) {
    int largest = i;
    int l = 2 * i + 1;
    int r = 2 * i + 2;

    if (l < n && arr[l] > arr[largest]) {
        largest = l;
    }

    if (r < n && arr[r] > arr[largest]) {
        largest = r;
    }

    if (largest != i) {
        intercambiar(arr, i, largest);
        heapify(arr, n, largest);
    }
}

void heapsort(PagedArray& arr, int n) {
    for (int i = n / 2 - 1; i >= 0; i--) {
        heapify(arr, n, i);
    }

    for (int i = n - 1; i > 0; i--) {
        intercambiar(arr, 0, i);
        heapify(arr, i, 0);
    }
}

bool verificarOrden(const string& filename) {
    ifstream archivo(filename, ios::binary);

    if (!archivo.is_open()) {
        cerr << "Error al abrir archivo para verificacion" << endl;
        return false;
    }

    int anterior, actual;

    if (!archivo.read((char*)&anterior, sizeof(int))) {
        archivo.close();
        return true;
    }

    while (archivo.read((char*)&actual, sizeof(int))) {
        if (actual < anterior) {
            cerr << "Desorden detectado: " << anterior << " > " << actual << endl;
            archivo.close();
            return false;
        }
        anterior = actual;
    }

    archivo.close();
    return true;
}

bool copiarArchivoBinario(const string& origen, const string& destino) {
    ifstream archivoEntrada(origen, ios::binary);
    if (!archivoEntrada.is_open()) {
        cerr << "Error al abrir el archivo de entrada" << endl;
        return false;
    }

    ofstream archivoSalida(destino, ios::binary);
    if (!archivoSalida.is_open()) {
        cerr << "Error al abrir el archivo de salida" << endl;
        return false;
    }

    archivoSalida << archivoEntrada.rdbuf();

    archivoEntrada.close();
    archivoSalida.close();
    return true;
}

void imprimirResumen(const string& nombreAlgoritmo, long long tiempoMs, PagedArray& arreglo){
    cout << "Tiempo de ejecucion: " << tiempoMs << " ms" << endl;
    cout << "Algoritmo utilizado: " << nombreAlgoritmo << endl;
    cout << "Total de elementos: " << arreglo.getTotalElementos() << endl;

    if (arreglo.getTotalElementos() > 0) {
        cout << "Primer elemento: " << (int)arreglo[0] << endl;
    }
    if (arreglo.getTotalElementos() > 1) {
        cout << "Segundo elemento: " << (int)arreglo[1] << endl;
    }
    if (arreglo.getTotalElementos() > 2) {
        cout << "Tercer elemento: " << (int)arreglo[2] << endl;
    }

    cout << "Hits: " << arreglo.getHits() << endl;
    cout << "Faults: " << arreglo.getFaults() << endl;
}

int main(int argc, char* argv[]) {
    long long tiempoMs = 0;
    int totalElementos = 0;
    int hitsFinales = 0;
    int faultsFinales = 0;
    if (argc < 11) {
        cerr << "Uso: sorter -input <archivo_entrada> -output <archivo_salida> -alg <algoritmo> -pageSize <page_size> -pageCount <page_count>" << endl;
        return 1;
    }

    string input = argv[2];
    string output = argv[4];
    string algoritmo = argv[6];
    int pageSize = atoi(argv[8]);
    int pageCount = atoi(argv[10]);

    string algoritmoUsado = "";
    auto inicio = high_resolution_clock::now();

    if (!copiarArchivoBinario(input, output)) {
        return 1;
    }

    PagedArray arreglo(output, pageSize, pageCount);
    int n = arreglo.size();

    if (algoritmo == "selection") {
        algoritmoUsado = "selectionsort";
        selectionsort(arreglo, n);
    } else if (algoritmo == "quick") {
        algoritmoUsado = "quicksort";
        quicksort(arreglo, 0, n - 1);
    } else if (algoritmo == "merge") {
        algoritmoUsado = "mergesort";
        mergesort(arreglo, 0, n - 1);
    } else if (algoritmo == "insertion") {
        algoritmoUsado = "insertionsort";
        insertionsort(arreglo, n);
    } else if (algoritmo == "heap") {
        algoritmoUsado = "heapsort";
        heapsort(arreglo, n);
    }else {
        cerr << "Algoritmo no reconocido" << endl;
        return 1;
    }

    auto fin = high_resolution_clock::now();
    tiempoMs = duration_cast<milliseconds>(fin - inicio).count();

    totalElementos = arreglo.getTotalElementos();
    hitsFinales = arreglo.getHits();
    faultsFinales = arreglo.getFaults();

    imprimirResumen(algoritmoUsado, tiempoMs, arreglo);



    bool ordenado = verificarOrden(output);
    if (ordenado) {
        cout << "Ordenado" << endl;
    } else {
        cout << "Desordenado" << endl;
    }


    return 0;
}