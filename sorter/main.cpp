#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
#include <vector>
using namespace std;
using namespace std::chrono;


bool copy(const string& archivoin, const string& archivoout) {
    ifstream input(archivoin, std::ios::binary);//abrir en lectura el archivo de origen
    ofstream output(archivoout, std::ios::binary);//abrir en escritura el archivo de destino

    if (!input.is_open() || !output.is_open()) {
        cout << "Error al abrir el archivo." << endl;
        return false;
    }

    output << input.rdbuf(); //Hace la copia del contenido

    if (!input.good() || !output.good()) {
        cout << "Error al copiar el archivo." << endl;
        return false;
    }

    cout<<"archivo copiado exitosamente"<<endl;
    return true;
}





int main() {
    auto start = high_resolution_clock::now();
    int indice = 3222;
    int pagesize = 4096;
    int intsperpage = pagesize / sizeof(int);
    int pagenumber = indice / intsperpage;
    int offset = indice % intsperpage;
    int bytes = offset * pagesize;
    int pagestartbyte = pagenumber * pagesize;
    int pagina[intsperpage];
    int offsetbytes = offset * sizeof(int);


    ifstream lectura("output.bin", std::ios::binary);
    //for (int i = 0; i < 134217728; i++) {
    lectura.seekg(pagestartbyte, std::ios::beg);
    for (int i = 0; i < intsperpage; i++) {
        int value;
        lectura.read((char*)&value, sizeof(int));
        pagina[i] = value;
    }
    //int value;
    //lectura.read((char*)&value, sizeof(int));
    //cout << value << endl;
    //}
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    cout << duration.count() << endl;
    cout << indice << endl;
    cout << pagesize << endl;
    cout << intsperpage << endl;
    cout << pagenumber << endl;
    cout << bytes << endl;
    cout << offset << endl;
    cout << pagina << endl;
    cout << pagina[offset] << endl;
    return 0;
}



class PagedArray {
public:
    int indice = 3222;
    int pagesize = 4096;
    int intsperpage = pagesize / sizeof(int);
    int Currentpagenumber = -1;
    ifstream lectura;
    vector<int> pagina;


    PagedArray(const string& filename) {
        lectura.open(filename, std::ios::binary);
        if (!lectura) {
            cerr << "Error al abrir el archivo " << filename << endl;
        }

        pagina.resize(intsperpage);
    }




    void CargarPagina(int pagenumber){

        int pagestartbyte = pagenumber * pagesize;

        lectura.seekg(pagestartbyte, std::ios::beg);
        for (int i = 0; i < intsperpage; i++) {
            int value;
            lectura.read((char*)&value, sizeof(int));
            pagina[i] = value;

        }
        Currentpagenumber = pagenumber;
    }

    int get(int indice) {
        int pagenumber = indice / intsperpage;
        int offset = indice % intsperpage;

        if (pagenumber != Currentpagenumber) {
            CargarPagina(pagenumber);

        }
        return pagina[offset];
    }
}
;
