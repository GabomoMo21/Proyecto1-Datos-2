#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <stdexcept>

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
            arreglo.set(indice, valor);//escribir el indicie
            return *this;
        }

        operator int() const {
            return arreglo.get(indice); //leer el indice
        }
    };

private:
    int intsPerPage;
    int pageCount;
    std::fstream archivo;

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
    static const int BUFFER_SIZE = 1 << 20;

    // cache del ultimo acceso
    int ultimoPageNumber;
    int ultimoSlot;

    int totalElementos;
    long long hits;
    long long faults;

public:
    // constructor
    PagedArray(const std::string& filename, int pageSize, int cantidadPaginas)
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

        //verificaciones de pagesize y pagecount
        if (intsPerPage <= 0) {
            throw std::invalid_argument("Pagesize tiene que ser mayor a 0");
        }

        if (pageCount <= 0) {
            throw std::invalid_argument("pageCount tiene que ser mayor a 0");
        }

        archivo.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        if (!archivo.is_open()) {
            throw std::runtime_error("error al abrir el archivo " + filename);
        }

        fileBuffer = new char[BUFFER_SIZE]; //buffer para el acceso al archivo
        archivo.rdbuf()->pubsetbuf(fileBuffer, BUFFER_SIZE);

        //calcular cantidad de bytes
        archivo.seekg(0, std::ios::end);
        std::streampos tamanoArchivo = archivo.tellg();

        if (tamanoArchivo < 0) {
            throw std::runtime_error("error al obtener el tamano del archivo");
        }

        totalElementos = (int)(tamanoArchivo / (std::streampos)sizeof(int));//cantidad de enteros
        archivo.seekg(0, std::ios::beg);

        // cuantas paginas logicas tiene el archivo
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

    // destructor, escribe paginas sucias y libera memoria
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

    Referencia operator[](int indice) { //devuelve la referencia
        return Referencia(*this, indice);
    }
    //stats de la paginacion
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

    // intercambia dos posiciones del arreglo
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
    void validarIndice(int indice) const { //revisa si el indice esta en el rango
        if (indice < 0 || indice >= totalElementos) {
            throw std::out_of_range("Indice fuera de rango: " + std::to_string(indice));
        }
    }
    //logica de paginacion
    int buscarSlotLibre() const { //revisa si hay campo en memoria
        for (int i = 0; i < pageCount; i++) {
            if (pageNumbers[i] == -1) {
                return i;
            }
        }
        return -1;
    }

    // calcula cuantos elementos tiene esa pagina
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

    // escribe una pagina solo si esta sucia
    void escribirPaginaSiEsNecesario(int slot) {
        if (slot < 0 || slot >= pageCount) {
            throw std::out_of_range("slot invalido");
        }

        if (pageNumbers[slot] == -1 || !dirtyFlags[slot]) {
            return;
        }

        int pageNumber = pageNumbers[slot];
        int elementosAEscribir = elementosValidosEnPagina(pageNumber);
        std::streampos pageStartByte = (std::streampos)pageNumber * intsPerPage * (std::streampos)sizeof(int);

        archivo.clear();
        archivo.seekp(pageStartByte, std::ios::beg);

        if (!archivo) {
            throw std::runtime_error("Error en seekp al escribir pagina");
        }

        archivo.write(reinterpret_cast<const char*>(paginas[slot]),
                      (std::streamsize)(elementosAEscribir * (int)sizeof(int)));

        if (!archivo) {
            throw std::runtime_error("Error al escribir bloque de pagina");
        }

        dirtyFlags[slot] = false;
    }

    // carga una pagina del archivo al slot indicado
    void cargarPagina(int pageNumber, int slot) {
        if (slot < 0 || slot >= pageCount) {
            throw std::out_of_range("Slot invalido al cargar pagina");
        }

        int elementosALeer = elementosValidosEnPagina(pageNumber);
        std::streampos pageStartByte = (std::streampos)pageNumber * intsPerPage * (std::streampos)sizeof(int);

        archivo.clear();
        archivo.seekg(pageStartByte, std::ios::beg);

        if (!archivo) {
            throw std::runtime_error("Error en seekg al cargar pagina");
        }

        if (elementosALeer > 0) {
            archivo.read(reinterpret_cast<char*>(paginas[slot]),
                         (std::streamsize)(elementosALeer * (int)sizeof(int)));

            std::streamsize bytesEsperados = (std::streamsize)(elementosALeer * (int)sizeof(int));
            if (archivo.gcount() != bytesEsperados) {
                throw std::runtime_error("Error al leer bloque");
            }
        }

        // rellena con ceros el resto si es la ultima pagina incompleta
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

    // elige a quien reemplazar (victima) usando clock
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

    // asegura que la pagina este cargada y devuelve el slot
    int asegurarPaginaCargada(int pageNumber) {
        // cache del ultimo acceso
        if (pageNumber == ultimoPageNumber && ultimoSlot != -1) {
            hits++;
            referenceBits[ultimoSlot] = true;
            return ultimoSlot;
        }

        // consulta directa con pageToSlot
        int slotExistente = pageToSlot[pageNumber];
        if (slotExistente != -1) {
            hits++;
            referenceBits[slotExistente] = true;
            ultimoPageNumber = pageNumber;
            ultimoSlot = slotExistente;
            return slotExistente;
        }

        faults++; //si no estaba en slot suma fault

        //buscar un campo libre
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

    // hace flush de todas las paginas sucias
    void flushTodasLasPaginas() {
        if (paginas == nullptr || pageNumbers == nullptr || dirtyFlags == nullptr) {
            return;
        }

        for (int i = 0; i < pageCount; i++) {
            escribirPaginaSiEsNecesario(i);
        }

        archivo.flush();
        if (!archivo) {
            throw std::runtime_error("Error al hacer flush final del archivo");
        }
    }
};

//funciones auxiliares

//archivo temporal para radixsort
bool crearArchivoTemporalConTamano(const std::string& nombre, int totalElementos) {
    std::ofstream archivo(nombre, std::ios::binary | std::ios::trunc);
    if (!archivo.is_open()) {
        std::cerr << "No se pudo crear el archivo temporal" << std::endl;
        return false;
    }

    if (totalElementos <= 0) {
        archivo.close();
        return true;
    }

    archivo.seekp((std::streamoff)totalElementos * sizeof(int) - 1, std::ios::beg);
    char cero = 0;
    archivo.write(&cero, 1);

    if (!archivo) {
        std::cerr << "No se pudo dimensionar el archivo temporal" << std::endl;
        archivo.close();
        return false;
    }

    archivo.close();
    return true;
}

// intercambia dos posiciones usando la funcion propia del paged array
void intercambiar(PagedArray& arreglo, int a, int b) {
    arreglo.swapIndices(a, b);
}

//algoritmos de ordenamiento

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

// merge de mergesort
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

// counting sort
//solo funciona si el rango es valido(smallrange)
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

    // valida el rango
    long long rangoReal = (long long)maxval - (long long)minval + 1;

    if (rangoReal <= 0 || rangoReal > 10000000LL) {
        throw std::runtime_error("Counting Sort no es viable para este rango de datos");
    }

    int rango = (int)rangoReal;
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

//introsort

// insertion sort para introsort
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
//heapsort para el introsort
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

//particion de introsort
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

// calcula la profundidad maxima para cambiar de quick a heap
int profundidadMaxima(int n) {
    return 2 * (int)log2(n);
}

//verifica el rango para elegir el algoritmo a usar
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

//introsort
void introsort(PagedArray& arreglo, int n) {
    if (n <= 1) {
        return;
    }

    int depthLimit = profundidadMaxima(n);
    introsortUtil(arreglo, 0, n - 1, depthLimit);
}

// radix sort usando archivo temporal y paged array
void radixsort(PagedArray& arr, int n, int pageSize, int pageCount, const std::string& archivoTemporal) {
    if (n <= 1) return;

    if (!crearArchivoTemporalConTamano(archivoTemporal, n)) {
        throw std::runtime_error("No se pudo crear archivo temporal");
    }

    PagedArray temp(archivoTemporal, pageSize, pageCount);

    PagedArray* src = &arr;
    PagedArray* dst = &temp;

    const int BASE = 32; //la base a utilizar por el radix sort
    const int BITS_POR_PASADA = 5;
    const int MASCARA = BASE - 1;

    for (int shift = 0; shift < 32; shift += BITS_POR_PASADA) {
        int conteo[BASE];
        int posiciones[BASE];

        for (int i = 0; i < BASE; i++) {
            conteo[i] = 0;
            posiciones[i] = 0;
        }

        // cuenta elementos por digito
        for (int i = 0; i < n; i++) {
            int valor = (*src)[i];
            unsigned int clave = ((unsigned int)valor) ^ 0x80000000u; //transforma el orden de enteros
            int digito = (clave >> shift) & MASCARA;
            conteo[digito]++;
        }

        // calcula posiciones iniciales
        for (int i = 1; i < BASE; i++) {
            posiciones[i] = posiciones[i - 1] + conteo[i - 1];
        }

        // distribuye los elementos
        for (int i = 0; i < n; i++) {
            int valor = (*src)[i];
            unsigned int clave = ((unsigned int)valor) ^ 0x80000000u;
            int digito = (clave >> shift) & MASCARA;

            int destino = posiciones[digito];
            (*dst)[destino] = valor;
            posiciones[digito]++;
        }

        std::swap(src, dst);
    }

    // si el resultado final quedo en el temporal, lo copia de vuelta
    if (src != &arr) {
        for (int i = 0; i < n; i++) {
            arr[i] = (*src)[i];
        }
    }
}

// revisa si el archivo final quedo ordenado

bool verificarOrden(const std::string& filename) {
    std::ifstream archivo(filename, std::ios::binary);

    if (!archivo.is_open()) {
        std::cerr << "Error al abrir archivo para verificacion" << std::endl;
        return false;
    }

    int anterior, actual;

    if (!archivo.read((char*)&anterior, sizeof(int))) {
        archivo.close();
        return true;
    }

    while (archivo.read((char*)&actual, sizeof(int))) {
        if (actual < anterior) {
            std::cerr << "Desorden detectado: " << anterior << " > " << actual << std::endl;
            archivo.close();
            return false;
        }
        anterior = actual;
    }

    archivo.close();
    return true;
}


//construye el archivo legible
//elimina la extension y la reemplaza por .txt
std::string construirNombreArchivoLegible(const std::string& output) {
    size_t punto = output.find_last_of('.');

    if (punto != std::string::npos) {
        return output.substr(0, punto) + ".txt";
    }

    return output + ".txt";
}

// genera un archivo de texto separado por comas a partir del binario ordenado
bool generarArchivoLegible(const std::string& archivoBinario, const std::string& archivoLegible) {
    std::ifstream archivoEntrada(archivoBinario, std::ios::binary);
    if (!archivoEntrada.is_open()) {
        std::cerr << "Error al abrir archivo binario" << std::endl;
        return false;
    }

    std::ofstream archivoSalida(archivoLegible);
    if (!archivoSalida.is_open()) {
        std::cerr << "Error al abrir archivo legible" << std::endl;
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

// copia el archivo de entrada al de salida para trabajar sobre la copia
bool copiarArchivoBinario(const std::string& origen, const std::string& destino) {
    std::ifstream archivoEntrada(origen, std::ios::binary);
    if (!archivoEntrada.is_open()) {
        std::cerr << "Error al abrir el archivo de entrada" << std::endl;
        return false;
    }

    std::ofstream archivoSalida(destino, std::ios::binary);
    if (!archivoSalida.is_open()) {
        std::cerr << "Error al abrir el archivo de salida" << std::endl;
        return false;
    }

    archivoSalida << archivoEntrada.rdbuf();

    archivoEntrada.close();
    archivoSalida.close();
    return true;
}

// imprimir resumen de datos
void imprimirResumen(const std::string& nombreAlgoritmo,
                     long long tiempoOrdenamientoMs,
                     PagedArray& arreglo) {
    std::cout << "Tiempo del algoritmo: " << tiempoOrdenamientoMs << " ms" << std::endl;
    std::cout << "Algoritmo: " << nombreAlgoritmo << std::endl;
    std::cout << "Hits: " << arreglo.getHits() << std::endl;
    std::cout << "Faults: " << arreglo.getFaults() << std::endl;
}

bool convertirEnteroPositivo(const char* texto, int& valor) {
    try {
        std::string s = texto;
        size_t pos = 0;
        int numero = std::stoi(s, &pos);

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

// valida los argumentos que se pasan por consola
bool validarArgumentos(int argc,
                       char* argv[],
                       std::string& input,
                       std::string& output,
                       std::string& algoritmo,
                       int& pageSize,
                       int& pageCount) {
    if (argc != 11) {
        std::cerr << "sorter -input <archivo_entrada> -output <archivo_salida> -alg <algoritmo> -pageSize <page_size> -pageCount <page_count>" << std::endl;
        return false;
    }

    if (std::string(argv[1]) != "-input") {
        std::cerr << "falta -input" << std::endl;
        return false;
    }

    if (std::string(argv[3]) != "-output") {
        std::cerr << "falta -output" << std::endl;
        return false;
    }

    if (std::string(argv[5]) != "-alg") {
        std::cerr << "falta -alg" << std::endl;
        return false;
    }

    if (std::string(argv[7]) != "-pageSize") {
        std::cerr << "falta -pageSize" << std::endl;
        return false;
    }

    if (std::string(argv[9]) != "-pageCount") {
        std::cerr << "falta -pageCount" << std::endl;
        return false;
    }

    input = argv[2];
    output = argv[4];
    algoritmo = argv[6];

    if (!std::filesystem::exists(input)) {
        std::cerr << "el archivo de input no existe" << std::endl;
        return false;
    }

    // evita que se intente ordenar sobre el mismo archivo original
    if (input == output) {
        std::cerr << "input y output no pueden ser el mismo archivo" << std::endl;
        return false;
    }

    if (!convertirEnteroPositivo(argv[8], pageSize)) {
        std::cerr << "pagesize debe ser un numero entero positivo " << std::endl;
        return false;
    }

    if (!convertirEnteroPositivo(argv[10], pageCount)) {
        std::cerr << "pageCount debe ser un numero entero positivo" << std::endl;
        return false;
    }

    if (algoritmo != "radix" &&
        algoritmo != "intro" &&
        algoritmo != "quick" &&
        algoritmo != "merge" &&
        algoritmo != "counting" &&
        algoritmo != "heap") {
        std::cerr << "algoritmo no reconocido" << std::endl;
        return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    long long tiempoOrdenamientoMs = 0;

    std::string input;
    std::string output;
    std::string algoritmo;
    int pageSize = 0;
    int pageCount = 0;

    //valida argumentos
    if (!validarArgumentos(argc, argv, input, output, algoritmo, pageSize, pageCount)) {
        return 1;
    }

    std::string algoritmoUsado = "";

    auto inicioTotal = std::chrono::high_resolution_clock::now();

    //copiar archivo
    if (!copiarArchivoBinario(input, output)) {
        return 1;
    }

    try {
        PagedArray arreglo(output, pageSize, pageCount); //crear el pagedarray
        int n = arreglo.size();

        auto inicioOrdenamiento = std::chrono::high_resolution_clock::now(); //inicia a correr tiempo

        //verificacion de algoritmo y lo corre
        if (algoritmo == "intro") {
            algoritmoUsado = "introsort";
            introsort(arreglo, n);
        } else if (algoritmo == "radix") {
            algoritmoUsado = "radixsort";
            std::string archivoTemporal = output + "radix.tmp.bin";
            radixsort(arreglo, n, pageSize, pageCount, archivoTemporal);
            std::filesystem::remove(archivoTemporal);
        } else if (algoritmo == "quick") {
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

        //calcular tiempo final
        auto finOrdenamiento = std::chrono::high_resolution_clock::now();
        tiempoOrdenamientoMs = std::chrono::duration_cast<std::chrono::milliseconds>(finOrdenamiento - inicioOrdenamiento).count();
        auto finTotal = std::chrono::high_resolution_clock::now();

        imprimirResumen(algoritmoUsado, tiempoOrdenamientoMs, arreglo);
    } catch (const std::exception& e) {
        std::cerr << "Error durante la ejecucion: " << e.what() << std::endl;
        return 1;
    }


    bool ordenado = verificarOrden(output);
    if (ordenado) {
        std::cout << "Ordenado" << std::endl;
    } else {
        std::cout << "Desordenado" << std::endl;
    }

    std::string outputLegible = construirNombreArchivoLegible(output);
    bool archivoLegibleGenerado = generarArchivoLegible(output, outputLegible);
    if (!archivoLegibleGenerado) {
        std::cerr << "No se pudo generar el archivo legible." << std::endl;
        return 1;
    }

    return 0;
}