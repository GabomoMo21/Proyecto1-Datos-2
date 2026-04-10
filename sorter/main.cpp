#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <stdexcept>

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
    int* memoriaPaginas;
    int* pageNumbers;
    bool* dirtyFlags;

    // estructuras de soporte
    bool* referenceBits;   // para clock
    int* pageToSlot;       // mapeo pagina
    int totalPages;
    int punteroClock;
    char* fileBuffer;
    static const int BUFFER_SIZE = 1<<20;

    // cache del ultimo acceso
    int ultimoPageNumber;
    int ultimoSlot;

    int totalElementos;
    long long hits;
    long long faults;

public:
    PagedArray(const string& filename, int pageSize, int cantidadPaginas)
        : intsPerPage(pageSize),
          pageCount(cantidadPaginas),
          paginas(nullptr),
          memoriaPaginas(nullptr),
          pageNumbers(nullptr),
          dirtyFlags(nullptr),
          referenceBits(nullptr),
          pageToSlot(nullptr),
          totalPages(0),
          punteroClock(0),
          ultimoPageNumber(-1),
          ultimoSlot(-1),
          fileBuffer(nullptr),
          totalElementos(0),
          hits(0),
          faults(0) {

        if (intsPerPage <= 0) {
            throw invalid_argument("Pagesize tiene que ser mayor a 0");
        }

        if (pageCount <= 0) {
            throw invalid_argument("pageCount tiene que ser mayor a 0");
        }

        archivo.open(filename, ios::in | ios::out | ios::binary);
        if (!archivo.is_open()) {
            throw runtime_error("error al abrir el archivo " + filename);
        }

        fileBuffer = new char[BUFFER_SIZE];
        archivo.rdbuf()->pubsetbuf(fileBuffer, BUFFER_SIZE);

        archivo.seekg(0, ios::end);
        streampos tamanoArchivo = archivo.tellg();

        if (tamanoArchivo < 0) {
            throw runtime_error("error al obtener el tamano del archivo");
        }

        totalElementos = (int)(tamanoArchivo / (streampos)sizeof(int));
        archivo.seekg(0, ios::beg);

        totalPages = (totalElementos + intsPerPage - 1) / intsPerPage;

        paginas = new int*[pageCount];
        memoriaPaginas = new int[pageCount * intsPerPage];

        for (int i = 0; i < pageCount; i++) {
            paginas[i] = memoriaPaginas + (i * intsPerPage);
        }

        try {
            pageNumbers = new int[pageCount];
            dirtyFlags = new bool[pageCount];
            referenceBits = new bool[pageCount];

            for (int i = 0; i < pageCount; i++) {
                pageNumbers[i] = -1;
                dirtyFlags[i] = false;
                referenceBits[i] = false;
            }

            pageToSlot = new int[totalPages];
            for (int i = 0; i < totalPages; i++) {
                pageToSlot[i] = -1;
            }

        } catch (...) {
            delete[] paginas;
            paginas = nullptr;

            delete[] memoriaPaginas;
            memoriaPaginas = nullptr;

            delete[] pageNumbers;
            pageNumbers = nullptr;

            delete[] dirtyFlags;
            dirtyFlags = nullptr;

            delete[] referenceBits;
            referenceBits = nullptr;

            delete[] pageToSlot;
            pageToSlot = nullptr;

            delete[] fileBuffer;
            fileBuffer = nullptr;

            if (archivo.is_open()) {
                archivo.close();
            }

            throw;
        }
    }

    PagedArray(const PagedArray&) = delete;
    PagedArray& operator=(const PagedArray&) = delete;

    ~PagedArray() {
        try {
            flushTodasLasPaginas();
        } catch (...) {
        }

        delete[] paginas;
        delete[] memoriaPaginas;
        delete[] pageNumbers;
        delete[] dirtyFlags;
        delete[] referenceBits;
        delete[] pageToSlot;
        delete[] fileBuffer;

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

    void swapIndices(int a, int b) {
        validarIndice(a);
        validarIndice(b);

        if (a == b) {
            return;
        }

        int pageA = a / intsPerPage;
        int offsetA = a % intsPerPage;

        int pageB = b / intsPerPage;
        int offsetB = b % intsPerPage;

        int slotA = asegurarPaginaCargada(pageA);
        int slotB = asegurarPaginaCargada(pageB);

        int temporal = paginas[slotA][offsetA];
        paginas[slotA][offsetA] = paginas[slotB][offsetB];
        paginas[slotB][offsetB] = temporal;

        dirtyFlags[slotA] = true;
        dirtyFlags[slotB] = true;
    }

    int get(int indice) {
        validarIndice(indice);

        int pageNumber = indice / intsPerPage;
        int offset = indice % intsPerPage;

        int slot = asegurarPaginaCargada(pageNumber);
        return paginas[slot][offset];
    }

    void set(int indice, int valor) {
        validarIndice(indice);

        int pageNumber = indice / intsPerPage;
        int offset = indice % intsPerPage;

        int slot = asegurarPaginaCargada(pageNumber);
        paginas[slot][offset] = valor;
        dirtyFlags[slot] = true;
    }

private:
    void validarIndice(int indice) const {
        if (indice < 0 || indice >= totalElementos) {
            throw out_of_range("Indice fuera de rango: " + to_string(indice));
        }
    }

    int buscarSlotLibre() const {
        for (int i = 0; i < pageCount; i++) {
            if (pageNumbers[i] == -1) {
                return i;
            }
        }
        return -1;
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

    void escribirPaginaSiEsNecesario(int slot) {
        if (slot < 0 || slot >= pageCount) {
            throw out_of_range("slot invalido");
        }

        if (pageNumbers[slot] == -1 || !dirtyFlags[slot]) {
            return;
        }

        int pageNumber = pageNumbers[slot];
        int elementosAEscribir = elementosValidosEnPagina(pageNumber);
        streampos pageStartByte = (streampos)pageNumber * intsPerPage * (streampos)sizeof(int);

        archivo.clear();
        archivo.seekp(pageStartByte, ios::beg);

        if (!archivo) {
            throw runtime_error("Error en seekp al escribir pagina");
        }

        archivo.write(reinterpret_cast<const char*>(paginas[slot]),
                      (streamsize)(elementosAEscribir * (int)sizeof(int)));

        if (!archivo) {
            throw runtime_error("Error al escribir bloque de pagina");
        }

        dirtyFlags[slot] = false;
    }

    void cargarPagina(int pageNumber, int slot) {
        if (slot < 0 || slot >= pageCount) {
            throw out_of_range("Slot invalido al cargar pagina");
        }

        int elementosALeer = elementosValidosEnPagina(pageNumber);
        streampos pageStartByte = (streampos)pageNumber * intsPerPage * (streampos)sizeof(int);

        archivo.clear();
        archivo.seekg(pageStartByte, ios::beg);

        if (!archivo) {
            throw runtime_error("Error en seekg al cargar pagina");
        }

        if (elementosALeer > 0) {
            archivo.read(reinterpret_cast<char*>(paginas[slot]),
                         (streamsize)(elementosALeer * (int)sizeof(int)));

            streamsize bytesEsperados = (streamsize)(elementosALeer * (int)sizeof(int));
            if (archivo.gcount() != bytesEsperados) {
                throw runtime_error("Error al leer bloque");
            }
        }

        for (int i = elementosALeer; i < intsPerPage; i++) {
            paginas[slot][i] = 0;
        }

        archivo.clear();

        pageNumbers[slot] = pageNumber;
        dirtyFlags[slot] = false;
        referenceBits[slot] = true;
        pageToSlot[pageNumber] = slot;

        ultimoPageNumber = pageNumber;
        ultimoSlot = slot;
    }

    int elegirVictimaClock() {
        while (true) {
            if (!referenceBits[punteroClock]) {
                int victima = punteroClock;
                punteroClock = (punteroClock + 1) % pageCount;
                return victima;
            }

            referenceBits[punteroClock] = false;
            punteroClock = (punteroClock + 1) % pageCount;
        }
    }

    int asegurarPaginaCargada(int pageNumber) {
        // cache del ultimo acceso
        if (pageNumber == ultimoPageNumber && ultimoSlot != -1) {
            hits++;
            referenceBits[ultimoSlot] = true;
            return ultimoSlot;
        }

        // consulta O(1)
        int slotExistente = pageToSlot[pageNumber];
        if (slotExistente != -1) {
            hits++;
            referenceBits[slotExistente] = true;
            ultimoPageNumber = pageNumber;
            ultimoSlot = slotExistente;
            return slotExistente;
        }

        faults++;

        int slotLibre = buscarSlotLibre();
        if (slotLibre != -1) {
            cargarPagina(pageNumber, slotLibre);
            return slotLibre;
        }

        int slotReemplazo = elegirVictimaClock();

        int paginaVieja = pageNumbers[slotReemplazo];
        if (paginaVieja != -1) {
            escribirPaginaSiEsNecesario(slotReemplazo);
            pageToSlot[paginaVieja] = -1;
        }

        cargarPagina(pageNumber, slotReemplazo);
        return slotReemplazo;
    }

    void flushTodasLasPaginas() {
        if (paginas == nullptr || pageNumbers == nullptr || dirtyFlags == nullptr) {
            return;
        }

        for (int i = 0; i < pageCount; i++) {
            escribirPaginaSiEsNecesario(i);
        }

        archivo.flush();
        if (!archivo) {
            throw runtime_error("Error al hacer flush final del archivo");
        }
    }
};

bool crearArchivoTemporalConTamano(const string& nombre, int totalElementos) {
    ofstream archivo(nombre, ios::binary | ios::trunc);
    if (!archivo.is_open()) {
        cerr << "No se pudo crear el archivo temporal" << endl;
        return false;
    }

    if (totalElementos <= 0) {
        archivo.close();
        return true;
    }

    archivo.seekp((streamoff)totalElementos * sizeof(int) - 1, ios::beg);
    char cero = 0;
    archivo.write(&cero, 1);

    if (!archivo) {
        cerr << "No se pudo dimensionar el archivo temporal" << endl;
        archivo.close();
        return false;
    }

    archivo.close();
    return true;
}

// Algoritmos de ordenamiento

void intercambiar(PagedArray& arreglo, int a, int b) {
    arreglo.swapIndices(a,b);
}

// selectionsort
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
        if (actual < pivot) {
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

// countingsort
void countingsort(PagedArray& arr, int n) {
    if (n <= 1) {
        return;
    }

    int minval = arr[0];
    int maxval = arr[0];

    for (int i = 1; i < n; i++) {
        int valor = arr[i];

        if (valor < minval) {
            minval = valor;
        }

        if (valor > maxval) {
            maxval = valor;
        }
    }

    int rango = maxval - minval + 1;
    int* conteo = new int[rango];

    for (int i = 0; i < rango; i++) {
        conteo[i] = 0;
    }

    for (int i = 0; i < n; i++) {
        conteo[arr[i] - minval]++;
    }

    int indice = 0;

    for (int i = 0; i < rango; i++) {
        while (conteo[i] > 0) {
            arr[indice] = i + minval;
            indice++;
            conteo[i]--;
        }
    }

    delete[] conteo;
}

// heapsort
void heapify(PagedArray& arr, int n, int i) {
    int largest = i;

    while (true) {
        int l = 2 * i + 1;
        int r = 2 * i + 2;
        largest = i;

        int valorLargest = arr[largest];

        if (l < n) {
            int valorL = arr[l];
            if (valorL > valorLargest) {
                largest = l;
                valorLargest = valorL;
            }
        }

        if (r < n) {
            int valorR = arr[r];
            if (valorR > valorLargest) {
                largest = r;
            }
        }

        if (largest == i) {
            break;
        }

        intercambiar(arr, i, largest);
        i = largest;
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

// introsort
void insertionSortRange(PagedArray& arreglo, int low, int high) {
    for (int i = low + 1; i <= high; i++) {
        int clave = arreglo[i];
        int j = i - 1;

        while (j >= low && arreglo[j] > clave) {
            arreglo[j + 1] = arreglo[j];
            j--;
        }

        arreglo[j + 1] = clave;
    }
}

void heapifyRange(PagedArray& arr, int low, int heapSize, int root) {
    int largest = root;
    int left = 2 * root + 1;
    int right = 2 * root + 2;

    if (left < heapSize && arr[low + left] > arr[low + largest]) {
        largest = left;
    }

    if (right < heapSize && arr[low + right] > arr[low + largest]) {
        largest = right;
    }

    if (largest != root) {
        intercambiar(arr, low + root, low + largest);
        heapifyRange(arr, low, heapSize, largest);
    }
}

void heapsortRange(PagedArray& arr, int low, int high) {
    int n = high - low + 1;

    for (int i = n / 2 - 1; i >= 0; i--) {
        heapifyRange(arr, low, n, i);
    }

    for (int i = n - 1; i > 0; i--) {
        intercambiar(arr, low, low + i);
        heapifyRange(arr, low, i, 0);
    }
}

int partitionIntro(PagedArray& arreglo, int low, int high) {
    int pivot = arreglo[high];
    int i = low - 1;

    for (int j = low; j < high; j++) {
        if (arreglo[j] <= pivot) {
            i++;
            intercambiar(arreglo, i, j);
        }
    }

    intercambiar(arreglo, i + 1, high);
    return i + 1;
}

int profundidadMaxima(int n) {
    return 2 * (int)log2(n);
}

void introsortUtil(PagedArray& arreglo, int low, int high, int depthLimit) {
    int size = high - low + 1;

    if (size <= 16) {
        insertionSortRange(arreglo, low, high);
        return;
    }

    if (depthLimit == 0) {
        heapsortRange(arreglo, low, high);
        return;
    }

    int pivot = partitionIntro(arreglo, low, high);

    introsortUtil(arreglo, low, pivot - 1, depthLimit - 1);
    introsortUtil(arreglo, pivot + 1, high, depthLimit - 1);
}

void introsort(PagedArray& arreglo, int n) {
    if (n <= 1) {
        return;
    }

    int depthLimit = profundidadMaxima(n);
    introsortUtil(arreglo, 0, n - 1, depthLimit);
}

void radixsort(PagedArray& arr, int n, int pageSize, int pageCount, const string& archivoTemporal) {
    if (n <= 1) return;

    if (!crearArchivoTemporalConTamano(archivoTemporal, n)) {
        throw runtime_error("No se pudo crear archivo temporal");
    }

    PagedArray temp(archivoTemporal, pageSize, pageCount);

    PagedArray* src = &arr;
    PagedArray* dst = &temp;

    const int BASE = 32;
    const int BITS_POR_PASADA = 5;
    const int MASCARA = BASE - 1; // 31 = 0x1F

    for (int shift = 0; shift < 32; shift += BITS_POR_PASADA) {
        int conteo[BASE];
        int posiciones[BASE];

        for (int i = 0; i < BASE; i++) {
            conteo[i] = 0;
            posiciones[i] = 0;
        }

        // contar
        for (int i = 0; i < n; i++) {
            int valor = (*src)[i];
            unsigned int clave = ((unsigned int)valor) ^ 0x80000000u;
            int digito = (clave >> shift) & MASCARA;
            conteo[digito]++;
        }

        // prefijos
        for (int i = 1; i < BASE; i++) {
            posiciones[i] = posiciones[i - 1] + conteo[i - 1];
        }

        // distribuir
        for (int i = 0; i < n; i++) {
            int valor = (*src)[i];
            unsigned int clave = ((unsigned int)valor) ^ 0x80000000u;
            int digito = (clave >> shift) & MASCARA;

            int destino = posiciones[digito];
            (*dst)[destino] = valor;
            posiciones[digito]++;
        }

        swap(src, dst);
    }

    if (src != &arr) {
        for (int i = 0; i < n; i++) {
            arr[i] = (*src)[i];
        }
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

string construirNombreArchivoLegible(const string& output) {
    size_t punto = output.find_last_of('.');

    if (punto != string::npos) {
        return output.substr(0, punto) + ".txt";
    }

    return output + ".txt";
}

bool generarArchivoLegible(const string& archivoBinario, const string& archivoLegible) {
    ifstream archivoEntrada(archivoBinario, ios::binary);
    if (!archivoEntrada.is_open()) {
        cerr << "Error al abrir archivo binario" << endl;
        return false;
    }

    ofstream archivoSalida(archivoLegible);
    if (!archivoSalida.is_open()) {
        cerr << "Error al abrir archivo legible" << endl;
        return false;
    }

    int value;
    bool primero = true;

    while (archivoEntrada.read((char*)&value, sizeof(int))) {
        if (!primero) {
            archivoSalida << ",";
        }

        archivoSalida << value;
        primero = false;
    }

    archivoEntrada.close();
    archivoSalida.close();
    return true;
}

bool guardarResultadoCSV(const string& nombreCSV,
                         const string& algoritmo,
                         const string& input,
                         const string& output,
                         int pageSize,
                         int pageCount,
                         int totalElementos,
                         long long tiempoOrdenamientoMs,
                         long long tiempoTotalMs,
                         long long hits,
                         long long faults,
                         bool ordenado,
                         bool archivoLegibleGenerado) {
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
        archivoCSV << "algoritmo,input,output,pageSize,pageCount,totalElementos,tiempo_ordenamiento_ms,tiempo_total_ms,hits,faults,ordenado,archivo_legible_generado\n";
    }

    archivoCSV << algoritmo << ","
               << input << ","
               << output << ","
               << pageSize << ","
               << pageCount << ","
               << totalElementos << ","
               << tiempoOrdenamientoMs << ","
               << tiempoTotalMs << ","
               << hits << ","
               << faults << ","
               << (ordenado ? "true" : "false") << ","
               << (archivoLegibleGenerado ? "true" : "false") << "\n";

    archivoCSV.close();
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

void imprimirResumen(const string& nombreAlgoritmo,
                     long long tiempoOrdenamientoMs,
                     long long tiempoTotalMs,
                     PagedArray& arreglo) {
    cout << "Tiempo del algoritmo: " << tiempoOrdenamientoMs << " ms" << endl;
    cout << "Tiempo del programa: " << tiempoTotalMs << " ms" << endl;
    cout << "Algoritmo: " << nombreAlgoritmo << endl;
    cout << "Total: " << arreglo.getTotalElementos() << endl;

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

bool convertirEnteroPositivo(const char* texto, int& valor) {
    try {
        string s = texto;
        size_t pos = 0;
        int numero = stoi(s, &pos);

        if (pos != s.size()) {
            return false;
        }

        if (numero <= 0) {
            return false;
        }

        valor = numero;
        return true;
    } catch (...) {
        return false;
    }
}

bool validarArgumentos(int argc,
                       char* argv[],
                       string& input,
                       string& output,
                       string& algoritmo,
                       int& pageSize,
                       int& pageCount) {
    if (argc != 11) {
        cerr << "sorter -input <archivo_entrada> -output <archivo_salida> -alg <algoritmo> -pageSize <page_size> -pageCount <page_count>" << endl;
        return false;
    }

    if (string(argv[1]) != "-input") {
        cerr << "Falta -input" << endl;
        return false;
    }

    if (string(argv[3]) != "-output") {
        cerr << "Falta -output" << endl;
        return false;
    }

    if (string(argv[5]) != "-alg") {
        cerr << "Falta -alg" << endl;
        return false;
    }

    if (string(argv[7]) != "-pageSize") {
        cerr << "Falta -pageSize" << endl;
        return false;
    }

    if (string(argv[9]) != "-pageCount") {
        cerr << "Falta -pageCount" << endl;
        return false;
    }

    input = argv[2];
    output = argv[4];
    algoritmo = argv[6];

    if (!filesystem::exists(input)) {
        cerr << "el archivo de input no existe" << endl;
        return false;
    }

    if (!convertirEnteroPositivo(argv[8], pageSize)) {
        cerr << "pageSize debe ser un numero entero positivo " << endl;
        return false;
    }

    if (!convertirEnteroPositivo(argv[10], pageCount)) {
        cerr << "pageCount debe ser un numero entero positivo" << endl;
        return false;
    }

    if (algoritmo != "radix" &&
        algoritmo != "intro" &&
        algoritmo != "quick" &&
        algoritmo != "merge" &&
        algoritmo != "counting" &&
        algoritmo != "heap") {
        cerr << "algoritmo no reconocido" << endl;
        return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    long long tiempoOrdenamientoMs = 0;
    long long tiempoTotalMs = 0;
    int totalElementos = 0;
    long long hitsFinales = 0;
    long long faultsFinales = 0;

    string input;
    string output;
    string algoritmo;
    int pageSize = 0;
    int pageCount = 0;

    if (!validarArgumentos(argc, argv, input, output, algoritmo, pageSize, pageCount)) {
        return 1;
    }

    string algoritmoUsado = "";

    auto inicioTotal = high_resolution_clock::now();

    if (!copiarArchivoBinario(input, output)) {
        return 1;
    }

    try {
        PagedArray arreglo(output, pageSize, pageCount);
        int n = arreglo.size();

        auto inicioOrdenamiento = high_resolution_clock::now();

        if (algoritmo == "intro") {
            algoritmoUsado = "introsort";
            introsort(arreglo, n);
        } else if (algoritmo == "radix") {
            algoritmoUsado = "radixsort";
            string archivoTemporal = output + "radix.tmp.bin";
            radixsort(arreglo, n, pageSize, pageCount, archivoTemporal);
            remove(archivoTemporal.c_str());
        }else if (algoritmo == "quick") {
            algoritmoUsado = "quicksort";
            quicksort(arreglo, 0, n - 1);
        } else if (algoritmo == "merge") {
            algoritmoUsado = "mergesort";
            mergesort(arreglo, 0, n - 1);
        } else if (algoritmo == "counting") {
            algoritmoUsado = "countingsort";
            countingsort(arreglo, n);
        } else if (algoritmo == "heap") {
            algoritmoUsado = "heapsort";
            heapsort(arreglo, n);
        }

        auto finOrdenamiento = high_resolution_clock::now();
        tiempoOrdenamientoMs = duration_cast<milliseconds>(finOrdenamiento - inicioOrdenamiento).count();

        totalElementos = arreglo.getTotalElementos();
        hitsFinales = arreglo.getHits();
        faultsFinales = arreglo.getFaults();

        auto finTotal = high_resolution_clock::now();
        tiempoTotalMs = duration_cast<milliseconds>(finTotal - inicioTotal).count();

        imprimirResumen(algoritmoUsado, tiempoOrdenamientoMs, tiempoTotalMs, arreglo);
    } catch (const exception& e) {
        cerr << "Error durante la ejecucion: " << e.what() << endl;
        return 1;
    }

    bool ordenado = verificarOrden(output);
    if (ordenado) {
        cout << "Ordenado" << endl;
    } else {
        cout << "Desordenado" << endl;
    }

    string outputLegible = construirNombreArchivoLegible(output);
    bool archivoLegibleGenerado = generarArchivoLegible(output, outputLegible);
    if (!archivoLegibleGenerado) {
        cerr << "No se pudo generar el archivo legible." << endl;
        return 1;
    }

    bool csvGuardado = guardarResultadoCSV("resultadossorter.csv",
                                           algoritmoUsado,
                                           input,
                                           output,
                                           pageSize,
                                           pageCount,
                                           totalElementos,
                                           tiempoOrdenamientoMs,
                                           tiempoTotalMs,
                                           hitsFinales,
                                           faultsFinales,
                                           ordenado,
                                           archivoLegibleGenerado);

    if (!csvGuardado) {
        cerr << "No se pudo guardar el CSV." << endl;
        return 1;
    }

    return 0;
}