#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>

#include "crow_all.h"
#include "json.hpp"
#include "baza_date.h"
#include "genuri.h"

using json = nlohmann::json;
using namespace std;

struct RecomandareSalvata {
    int itemId;
    string titlu;
    vector<string> genuri;
    int an;
    double rating;
    string tip;
    string artist;
    string autor;
    double similaritate;
    string dataGenerare;
    string tipRecomandare;
    bool evaluata;
    double ratingUtilizator;
    string dataEvaluare;
};

struct Utilizator {
     string numeUtilizator;
    string parola;
    set<int> itemuriPreferate;
    set<int> istoricRecomandari;
    map<int, double> ratinguriItem;
    map<int, pair<double, string>> recomandariRatinguite;
    // AdaugДѓ aceastДѓ linie:
    map<string, vector<RecomandareSalvata>> recomandariSalvate;
};

struct Recomandare {
    int itemId;
    double similaritate;
    Recomandare(int id, double sim) : itemId(id), similaritate(sim) {}
};

struct ItemRating {
    int itemId;
    double ratingMediu;
    int numarRatinguri;
    string titlu;
    string tip;
    
    ItemRating(int id, double rating, int numar, string titlu, string tip) 
        : itemId(id), ratingMediu(rating), numarRatinguri(numar), titlu(titlu), tip(tip) {}
};

class SistemRecomandare {
private:
    vector<Item> bazaDate;
    map<string, Utilizator> utilizatori;
    map<int, vector<double>> caracteristiciItem;
    set<string> toateGenurile;
    int proximulId;
    
    const string FISIER_UTILIZATORI = "utilizatori.json";
    const string FISIER_BAZA_DATE = "baza_date.json";

    // HEAP SORT
    void heapify(vector<Recomandare>& arr, int n, int i) {
        int maiMare = i;
        int stanga = 2 * i + 1;
        int dreapta = 2 * i + 2;

        if (stanga < n && arr[stanga].similaritate > arr[maiMare].similaritate)
            maiMare = stanga;
        if (dreapta < n && arr[dreapta].similaritate > arr[maiMare].similaritate)
            maiMare = dreapta;

        if (maiMare != i) {
            swap(arr[i], arr[maiMare]);
            heapify(arr, n, maiMare);
        }
    }

    void heapSort(vector<Recomandare>& arr) {
        int n = arr.size();
        for (int i = n / 2 - 1; i >= 0; i--)
            heapify(arr, n, i);
        for (int i = n - 1; i > 0; i--) {
            swap(arr[0], arr[i]);
            heapify(arr, i, 0);
        }
    }

    double similaritateCosinus(const vector<double>& vec1, const vector<double>& vec2) {
        if (vec1.size() != vec2.size()) return 0.0;
        
        double produsScalar = 0.0;
        double norma1 = 0.0;
        double norma2 = 0.0;
        
        for (size_t i = 0; i < vec1.size(); i++) {
            produsScalar += vec1[i] * vec2[i];
            norma1 += vec1[i] * vec1[i];
            norma2 += vec2[i] * vec2[i];
        }
        
        if (norma1 == 0.0 || norma2 == 0.0) return 0.0;
        return produsScalar / (sqrt(norma1) * sqrt(norma2));
    }

    // Calculează preferințele pe genuri pentru un utilizator.
// Rezultat: map<gen, scor> în [-1, 1], unde:
//   > 0  = gen plăcut
//   ~ 0  = neutru
//   < 0  = gen nepreferat (penalizat)
map<string, double> calculeazaPreferinteGenuri(const Utilizator& user) {
    map<string, double> scorNet;   // suma +1 / -1 / +0.3 pe gen
    map<string, int>    cnt;       // câte contribuții are fiecare gen

    // 1) Ratinguri – principalele semnale
    for (const auto& p : user.ratinguriItem) {
        int itemId = p.first;
        double r   = p.second; // 1–10

        Item* item = gasestItemDupaId(itemId);
        if (!item) continue;

        double contrib = 0.0;
        if (r >= 7.0)      contrib = 1.0;   // like
        else if (r <= 4.0) contrib = -1.0;  // dislike
        else               contrib = 0.0;   // neutru

        if (contrib == 0.0) continue;

        for (const string& gen : item->genuri) {
            scorNet[gen] += contrib;
            cnt[gen]     += 1;
        }
    }

    // 2) Favorite fără rating – semnal slab pozitiv
    for (int itemId : user.itemuriPreferate) {
        if (user.ratinguriItem.find(itemId) != user.ratinguriItem.end())
            continue; // deja acoperit de rating

        Item* item = gasestItemDupaId(itemId);
        if (!item) continue;

        for (const string& gen : item->genuri) {
            scorNet[gen] += 0.3;  // favorit fără rating = like slab
            cnt[gen]     += 1;
        }
    }

    // 3) Normalizăm în [-1, 1]
    map<string, double> preferinte;
    for (const auto& p : scorNet) {
        const string& gen = p.first;
        double net = p.second;
        int c = max(1, cnt[gen]);

        double pref = net / (double)c; // ~[-1,1] dacă like/dislike se compensează
        if (pref >  1.0) pref =  1.0;
        if (pref < -1.0) pref = -1.0;

        preferinte[gen] = pref;
    }

    return preferinte;
}


    vector<double> construiesteProfilUtilizator(const Utilizator& user) {
    // Dimensiunea vectorului de caracteristici (luăm dimensiunea primului item)
    size_t dim = 0;
    if (!caracteristiciItem.empty()) {
        dim = caracteristiciItem.begin()->second.size();
    }

    vector<double> profil(dim, 0.0);
    if (dim == 0) {
        return profil; // nu avem caracteristici încă
    }

    // 0) Pregătim informația despre genuri
    //    - ordinea genurilor trebuie să fie aceeași ca în genereazaVectorCaracteristici
    vector<string> ordineGenuri(toateGenurile.begin(), toateGenurile.end());
    size_t numGenuri = ordineGenuri.size();

    // Preferințele pe genuri ([-1, 1])
    map<string, double> preferinteGenuri = calculeazaPreferinteGenuri(user);

    double sumaPonderi = 0.0;

    // 1. Construim o listă unificată de itemi relevanți pentru profil:
    //    - toți itemii din favorite
    //    - toți itemii cu rating (chiar dacă nu sunt în favorite, deși la tine rating => favorite)
    set<int> itemIds;

    for (int id : user.itemuriPreferate) {
        itemIds.insert(id);
    }
    for (const auto& p : user.ratinguriItem) {
        itemIds.insert(p.first);
    }

    // 2. Agregăm vectorii de caracteristici ai acestor itemi într-un profil mediu ponderat
    for (int itemId : itemIds) {
        auto itVec = caracteristiciItem.find(itemId);
        if (itVec == caracteristiciItem.end())
            continue; // nu avem vector de caracteristici pentru acest item

        const std::vector<double>& v = itVec->second;

        // Ponderea: dacă există rating, folosim rating/10, altfel 0.7 „doar favorit”
        double pondere = 0.7;
        auto itRating = user.ratinguriItem.find(itemId);
        if (itRating != user.ratinguriItem.end()) {
            double r = itRating->second;      // 1–10
            pondere = std::max(0.1, std::min(1.0, r / 10.0));
        }

        for (size_t i = 0; i < dim; ++i) {
            profil[i] += v[i] * pondere;
        }
        sumaPonderi += pondere;
    }

    // 3. Normalizăm profilul la media ponderată
    if (sumaPonderi > 0.0) {
        for (double& x : profil) {
            x /= sumaPonderi;
        }
    }

    // 4. APLICĂM BONUSURI/PENALIZĂRI PE GENURI
    //    Primele `numGenuri` componente sunt one-hot pe genuri
    for (size_t i = 0; i < numGenuri && i < profil.size(); ++i) {
        const string& gen = ordineGenuri[i];
        auto itPref = preferinteGenuri.find(gen);
        if (itPref == preferinteGenuri.end()) continue;

        double pref = itPref->second; // [-1, 1]
        double factor = 1.0;

        if (pref > 0.0) {
            // gen plăcut: până la +40% boost
            factor = 1.0 + 0.4 * pref;  // [1.0, 1.4]
        } else if (pref < 0.0) {
            // gen nepreferat: până la -60% (adică factor minim 0.4)
            factor = 1.0 + 0.6 * pref;  // [0.4, 1.0]
        }

        if (factor < 0.0) factor = 0.0; // siguranță
        profil[i] *= factor;
    }

    return profil;
}



    double similaritateUtilizatori(const Utilizator& user1, const Utilizator& user2) {
    // Construim profilurile content-based pentru cei doi utilizatori
    vector<double> profil1 = construiesteProfilUtilizator(user1);
    vector<double> profil2 = construiesteProfilUtilizator(user2);

    if (profil1.empty() || profil2.empty())
        return 0.0;

    double sim = similaritateCosinus(profil1, profil2);

    // Cosine poate ieși ușor negativ dacă există componente opuse,
    // dar pentru recomandări folosim doar [0,1].
    if (sim < 0.0) sim = 0.0;

    return sim;
}



    // întoarce top-k utilizatori similari cu numeUtilizator
vector<pair<string, double>> gasestUtilizatoriSimilari(const string& numeUtilizator, int k) {
    vector<pair<string, double>> similariUtilizatori;
    
    // dacă userul nu există, întoarcem listă goală
    if (utilizatori.find(numeUtilizator) == utilizatori.end()) {
        return similariUtilizatori;
    }
    
    const Utilizator& utilizatorCurent = utilizatori[numeUtilizator];
    
    // parcurgem toți ceilalți utilizatori
    for (const auto& pereche : utilizatori) {
        if (pereche.first == numeUtilizator) continue; // sari peste el însuși
        
        double similaritate = similaritateUtilizatori(utilizatorCurent, pereche.second);
        
        // păstrăm doar utilizatorii cu similaritate pozitivă
        if (similaritate > 0.0) {
            similariUtilizatori.push_back({pereche.first, similaritate});
        }
    }
    
    // sortăm descrescător după similaritate
    sort(similariUtilizatori.begin(), similariUtilizatori.end(),
         [](const pair<string, double>& a, const pair<string, double>& b) {
             return a.second > b.second;
         });
    
    // păstrăm doar primii k
    int count = min(k, (int)similariUtilizatori.size());
    return vector<pair<string, double>>(similariUtilizatori.begin(),
                                        similariUtilizatori.begin() + count);
}




    vector<Recomandare> gasesteCeiMaiApropriatiKVecini(int itemId, int k, const set<int>& excludeIds, const string& tip) {
        vector<Recomandare> recomandari;
        
        if (caracteristiciItem.find(itemId) == caracteristiciItem.end())
            return recomandari;
        
        vector<double> caracteristiciTinta = caracteristiciItem[itemId];
        
        for (const auto& pereche : caracteristiciItem) {
            int altId = pereche.first;
            if (altId == itemId || excludeIds.find(altId) != excludeIds.end())
                continue;
            
            Item* itemAlt = gasestItemDupaId(altId);
            if (!itemAlt || itemAlt->tip != tip)
                continue;
            
            double similaritate = similaritateCosinus(caracteristiciTinta, pereche.second);
            recomandari.push_back(Recomandare(altId, similaritate));
        }
        
        heapSort(recomandari);
        
        vector<Recomandare> topK;
        int count = min(k, (int)recomandari.size());
        for (int i = recomandari.size() - 1; i >= recomandari.size() - count && i >= 0; i--) {
            topK.push_back(recomandari[i]);
        }
        
        return topK;
    }

    vector<double> genereazaVectorCaracteristici(const Item& item) {
        vector<double> caracteristici;
        
        for (const string& gen : toateGenurile) {
            bool areGen = find(item.genuri.begin(), item.genuri.end(), gen) != item.genuri.end();
            caracteristici.push_back(areGen ? 1.0 : 0.0);
        }
        
        caracteristici.push_back(item.rating / 10.0);
        caracteristici.push_back((item.an - 1900) / 100.0);
        caracteristici.push_back(item.tip == "film" ? 1.0 : 0.0);
        caracteristici.push_back(item.tip == "carte" ? 1.0 : 0.0);
        caracteristici.push_back(item.tip == "muzica" ? 1.0 : 0.0);
        
        return caracteristici;
    }

    void construiesteVectoriCaracteristici() {
        // incarca genurile din fisierul header
        vector<string> genuriDisponibile = getToateGenurile();
        toateGenurile.clear();
        toateGenurile.insert(genuriDisponibile.begin(), genuriDisponibile.end());
        
        caracteristiciItem.clear();
        for (const auto& item : bazaDate) {
            caracteristiciItem[item.id] = genereazaVectorCaracteristici(item);
        }
    }

    Item* gasestItemDupaId(int id) {
        for (auto& item : bazaDate) {
            if (item.id == id) return &item;
        }
        return nullptr;
    }

    void incarcaUtilizatori() {
    ifstream fisier(FISIER_UTILIZATORI);
    if (!fisier.is_open()) return;
    
    json j;
    fisier >> j;
    fisier.close();
    
    utilizatori.clear();
    for (const auto& userJson : j["utilizatori"]) {
        Utilizator user;
        user.numeUtilizator = userJson["numeUtilizator"];
        user.parola = userJson["parola"];
        user.itemuriPreferate = userJson["itemuriPreferate"].get<set<int>>();
        user.istoricRecomandari = userJson["istoricRecomandari"].get<set<int>>();
        
        if (userJson.contains("ratinguriItem")) {
            for (const auto& rating : userJson["ratinguriItem"].items()) {
                user.ratinguriItem[stoi(rating.key())] = rating.value();
            }
        }
        
        if (userJson.contains("recomandariRatinguite")) {
            for (const auto& rec : userJson["recomandariRatinguite"].items()) {
                int itemId = stoi(rec.key());
                double rating = rec.value()["rating"];
                string data = rec.value()["data"];
                user.recomandariRatinguite[itemId] = {rating, data};
            }
        }
        
        // incarca recomandarile salvate 
         if (userJson.contains("recomandariSalvate")) {
            for (const auto& sesiune : userJson["recomandariSalvate"].items()) {
                string timestamp = sesiune.key();
                vector<RecomandareSalvata> recomandari;
                
                for (const auto& rec : sesiune.value()) {
                    RecomandareSalvata recomandare;
                    recomandare.itemId = rec["itemId"];
                    recomandare.titlu = rec.value("titlu", "");
                    recomandare.genuri = rec.value("genuri", vector<string>());
                    recomandare.an = rec.value("an", 0);
                    recomandare.rating = rec.value("rating", 0.0);
                    recomandare.tip = rec.value("tip", "");
                    recomandare.artist = rec.value("artist", "");
                    recomandare.autor = rec.value("autor", "");
                    recomandare.similaritate = rec.value("similaritate", 0.0);
                    recomandare.dataGenerare = rec.value("dataGenerare", "");
                    recomandare.tipRecomandare = rec.value("tipRecomandare", "");
                    recomandare.evaluata = rec.value("evaluata", false);
                    recomandare.ratingUtilizator = rec.value("ratingUtilizator", 0.0);
                    recomandare.dataEvaluare = rec.value("dataEvaluare", "");
                    
                    recomandari.push_back(recomandare);
        }
         user.recomandariSalvate[timestamp] = recomandari;
            }
        }
        
        utilizatori[user.numeUtilizator] = user;
    }
}

    void salveazaUtilizatori() {
    json j;
    j["utilizatori"] = json::array(); // ✅ INITIALIZEAZĂ EXPLICIT
    
    for (const auto& pereche : utilizatori) {
        const Utilizator& user = pereche.second;
        json userJson;
        
        userJson["numeUtilizator"] = user.numeUtilizator;
        userJson["parola"] = user.parola;
        userJson["itemuriPreferate"] = user.itemuriPreferate;
        userJson["istoricRecomandari"] = user.istoricRecomandari;
        
        // Salvează ratingurile
        json ratinguriJson;
        for (const auto& rating : user.ratinguriItem) {
            ratinguriJson[to_string(rating.first)] = rating.second;
        }
        userJson["ratinguriItem"] = ratinguriJson;
        
        // Salvează recomandările ratinguite
        json recomandariJson;
        for (const auto& rec : user.recomandariRatinguite) {
            json recJson;
            recJson["rating"] = rec.second.first;
            recJson["data"] = rec.second.second;
            recomandariJson[to_string(rec.first)] = recJson;
        }
        userJson["recomandariRatinguite"] = recomandariJson;
        
        // Salvează recomandările salvate
        json recomandariSalvateJson;
        for (const auto& sesiune : user.recomandariSalvate) {
            json sesiuneJson = json::array();
            for (const auto& rec : sesiune.second) {
                json recJson;
                recJson["itemId"] = rec.itemId;
                recJson["titlu"] = rec.titlu;
                recJson["genuri"] = rec.genuri;
                recJson["an"] = rec.an;
                recJson["rating"] = rec.rating;
                recJson["tip"] = rec.tip;
                recJson["artist"] = rec.artist;
                recJson["autor"] = rec.autor;
                recJson["similaritate"] = rec.similaritate;
                recJson["dataGenerare"] = rec.dataGenerare;
                recJson["tipRecomandare"] = rec.tipRecomandare;
                recJson["evaluata"] = rec.evaluata;
                recJson["ratingUtilizator"] = rec.ratingUtilizator;
                recJson["dataEvaluare"] = rec.dataEvaluare;
                
                sesiuneJson.push_back(recJson);
            }
            recomandariSalvateJson[sesiune.first] = sesiuneJson;
        }
        userJson["recomandariSalvate"] = recomandariSalvateJson;
        
        j["utilizatori"].push_back(userJson);
    }
    
    ofstream fisier(FISIER_UTILIZATORI);
    fisier << j.dump(4);
    fisier.close();
}
    void initializeazaBazaDateImplicita() {
        bazaDate = getBazaDateInitiala();
        proximulId = 301;
    }

    vector<string> valideazaGenuri(const vector<string>& genuriSelectate) {
        vector<string> genuriValide;
        
        for (const string& gen : genuriSelectate) {
            if (toateGenurile.find(gen) != toateGenurile.end()) {
                genuriValide.push_back(gen);
            }
        }
        
        return genuriValide;
    }

    // Functie pentru calcularea ratingurilor medii ale item-ilor
    map<int, pair<double, int>> calculeazaRatinguriMedii() {
        map<int, pair<double, int>> ratinguri;
        
        for (const auto& pereche : utilizatori) {
            for (const auto& rating : pereche.second.ratinguriItem) {
                int itemId = rating.first;
                double ratingValue = rating.second;
                
                ratinguri[itemId].first += ratingValue;
                ratinguri[itemId].second += 1;
            }
        }
        
        return ratinguri;
    }


       void incarcaBazaDate() {
        ifstream fisier(FISIER_BAZA_DATE);
        if (fisier.is_open()) {
            json j;
            fisier >> j;
            fisier.close();
            
            bazaDate.clear();
            for (const auto& itemJson : j["items"]) {
                Item item;
                item.id = itemJson["id"];
                item.titlu = itemJson["titlu"];
                item.genuri = itemJson["genuri"].get<vector<string>>();
                item.an = itemJson["an"];
                item.rating = itemJson["rating"];
                item.tip = itemJson["tip"];
                item.artist = itemJson.value("artist", "");
                item.autor = itemJson.value("autor", "");
                bazaDate.push_back(item);
                
                // Actualizează proximulId
                if (item.id >= proximulId) {
                    proximulId = item.id + 1;
                }
            }
        } else {
            // Dacă fișierul nu există, folosește datele implicite
            initializeazaBazaDateImplicita();
        }
    }

    void salveazaBazaDate() {
        json j;
        j["items"] = json::array();
        
        for (const auto& item : bazaDate) {
            json itemJson;
            itemJson["id"] = item.id;
            itemJson["titlu"] = item.titlu;
            itemJson["genuri"] = item.genuri;
            itemJson["an"] = item.an;
            itemJson["rating"] = item.rating;
            itemJson["tip"] = item.tip;
            if (!item.artist.empty()) itemJson["artist"] = item.artist;
            if (!item.autor.empty()) itemJson["autor"] = item.autor;
            
            j["items"].push_back(itemJson);
        }
        
        ofstream fisier(FISIER_BAZA_DATE);
        fisier << j.dump(4);
        fisier.close();
    }

public:
    SistemRecomandare() : proximulId(1) {
        incarcaBazaDate();
        incarcaUtilizatori();
        construiesteVectoriCaracteristici();
    }

    ~SistemRecomandare() {
        salveazaUtilizatori();
        salveazaBazaDate();
    }



    json inregistrareUtilizatorAPI(const string& nume, const string& parola) {
        json response;
        
        if (utilizatori.find(nume) != utilizatori.end()) {
            response["success"] = false;
            response["message"] = "Acest nume de utilizator este deja ocupat!";
            return response;
        }
        
        Utilizator userNou;
        userNou.numeUtilizator = nume;
        userNou.parola = parola;
        utilizatori[nume] = userNou;
        salveazaUtilizatori();
        
        response["success"] = true;
        response["message"] = "Cont creat cu succes!";
        return response;
    }

    json autentificareUtilizatorAPI(const string& nume, const string& parola) {
        json response;
        
        if (utilizatori.find(nume) == utilizatori.end()) {
            response["success"] = false;
            response["message"] = "Utilizator inexistent!";
            return response;
        }
        
        if (utilizatori[nume].parola != parola) {
            response["success"] = false;
            response["message"] = "ParolДѓ incorectДѓ!";
            return response;
        }
        
        response["success"] = true;
        response["message"] = "Autentificare reuИ™itДѓ!";
        response["username"] = nume;
        return response;
    }

    json getBazaDateAPI(const string& filtruTip = "") {
        json response;
        json items = json::array();
        
        for (const auto& item : bazaDate) {
            if (!filtruTip.empty() && item.tip != filtruTip) continue;
            
            json itemJson;
            itemJson["id"] = item.id;
            itemJson["titlu"] = item.titlu;
            itemJson["genuri"] = item.genuri;
            itemJson["an"] = item.an;
            itemJson["rating"] = item.rating;
            itemJson["tip"] = item.tip;
            if (!item.artist.empty()) itemJson["artist"] = item.artist;
            if (!item.autor.empty()) itemJson["autor"] = item.autor;
            
            items.push_back(itemJson);
        }
        
        response["success"] = true;
        response["items"] = items;
        response["total"] = items.size();
        return response;
    }

    json cautaInBazaDateAPI(const string& cuvinteCheie) {
        json response;
        json items = json::array();
        
        string cuvinteLower = cuvinteCheie;
        transform(cuvinteLower.begin(), cuvinteLower.end(), cuvinteLower.begin(), ::tolower);
        
        for (const auto& item : bazaDate) {
            string titluLower = item.titlu;
            transform(titluLower.begin(), titluLower.end(), titluLower.begin(), ::tolower);
            
            if (titluLower.find(cuvinteLower) != string::npos) {
                json itemJson;
                itemJson["id"] = item.id;
                itemJson["titlu"] = item.titlu;
                itemJson["genuri"] = item.genuri;
                itemJson["an"] = item.an;
                itemJson["rating"] = item.rating;
                itemJson["tip"] = item.tip;
                if (!item.artist.empty()) itemJson["artist"] = item.artist;
                if (!item.autor.empty()) itemJson["autor"] = item.autor;
                
                items.push_back(itemJson);
            }
        }
        
        response["success"] = true;
        response["items"] = items;
        response["total"] = items.size();
        return response;
    }

        // API intern / debug: profil de genuri pentru un utilizator
    json getProfilUtilizatorDebugAPI(const string& username) {
        json response;

        // Verifică dacă utilizatorul există
        if (utilizatori.find(username) == utilizatori.end()) {
            response["success"] = false;
            response["message"] = "Utilizator inexistent!";
            return response;
        }

        const Utilizator& user = utilizatori[username];

        // Folosim funcția de preferințe pe genuri deja existentă
        map<string, double> preferinteGenuri = calculeazaPreferinteGenuri(user);

        // Construim lista completă de genuri, în ordinea globală
        vector<string> genuriOrdine(toateGenurile.begin(), toateGenurile.end());
        sort(genuriOrdine.begin(), genuriOrdine.end());

        json genuriJson = json::array();

        for (const string& gen : genuriOrdine) {
            double scor = 0.0;
            auto it = preferinteGenuri.find(gen);
            if (it != preferinteGenuri.end()) {
                scor = it->second; // [-1, 1]
            }

            json genJson;
            genJson["gen"] = gen;
            genJson["scor"] = scor;

            // O mică descriere textuală, utilă în UI
            string descriere;
            if (scor > 0.5)        descriere = "foarte preferat";
            else if (scor > 0.1)   descriere = "preferat";
            else if (scor >= -0.1) descriere = "neutru";
            else if (scor > -0.5)  descriere = "nepreferat";
            else                   descriere = "evitat";

            genJson["descriere"] = descriere;

            genuriJson.push_back(genJson);
        }

        // Pentru confort putem trimite și top 5 genuri pozitive / negative separate
        vector<pair<string, double>> pozitive;
        vector<pair<string, double>> negative;

        for (const auto& p : preferinteGenuri) {
            if (p.second > 0.0) pozitive.push_back(p);
            if (p.second < 0.0) negative.push_back(p);
        }

        sort(pozitive.begin(), pozitive.end(),
             [](const auto& a, const auto& b) { return a.second > b.second; });
        sort(negative.begin(), negative.end(),
             [](const auto& a, const auto& b) { return a.second < b.second; });

        json topPozitive = json::array();
        json topNegative = json::array();

        int maxTop = 5;
        for (int i = 0; i < (int)pozitive.size() && i < maxTop; ++i) {
            json o;
            o["gen"] = pozitive[i].first;
            o["scor"] = pozitive[i].second;
            topPozitive.push_back(o);
        }
        for (int i = 0; i < (int)negative.size() && i < maxTop; ++i) {
            json o;
            o["gen"] = negative[i].first;
            o["scor"] = negative[i].second;
            topNegative.push_back(o);
        }

        response["success"] = true;
        response["username"] = username;
        response["genuri"] = genuriJson;
        response["topPozitive"] = topPozitive;
        response["topNegative"] = topNegative;
        response["totalGenuri"] = genuriJson.size();

        return response;
    }


    json adaugaLaFavoriteAPI(const string& username, int itemId) {
        json response;
        
        if (utilizatori.find(username) == utilizatori.end()) {
            response["success"] = false;
            response["message"] = "Utilizator inexistent!";
            return response;
        }
        
        Item* item = gasestItemDupaId(itemId);
        if (!item) {
            response["success"] = false;
            response["message"] = "Item inexistent!";
            return response;
        }
        
        utilizatori[username].itemuriPreferate.insert(itemId);
        salveazaUtilizatori();
        
        response["success"] = true;
        response["message"] = "AdДѓugat la favorite!";
        return response;
    }

    json eliminaDinFavoriteAPI(const string& username, int itemId) {
    json response;
    
    if (utilizatori.find(username) == utilizatori.end()) {
        response["success"] = false;
        response["message"] = "Utilizator inexistent!";
        return response;
    }
    
    Item* item = gasestItemDupaId(itemId);
    if (!item) {
        response["success"] = false;
        response["message"] = "Item inexistent!";
        return response;
    }
    
    auto& favorite = utilizatori[username].itemuriPreferate;
    auto it = favorite.find(itemId);
    if (it == favorite.end()) {
        response["success"] = false;
        response["message"] = "Item-ul nu este Г®n lista de favorite!";
        return response;
    }
    
    favorite.erase(it);
    salveazaUtilizatori();
    
    response["success"] = true;
    response["message"] = "Item eliminat din favorite!";
    return response;
}
    json getFavoriteAPI(const string& username) {
        json response;
        
        if (utilizatori.find(username) == utilizatori.end()) {
            response["success"] = false;
            response["message"] = "Utilizator inexistent!";
            return response;
        }
        
        json items = json::array();
        for (int id : utilizatori[username].itemuriPreferate) {
            Item* item = gasestItemDupaId(id);
            if (!item) continue;
            
            json itemJson;
            itemJson["id"] = item->id;
            itemJson["titlu"] = item->titlu;
            itemJson["genuri"] = item->genuri;
            itemJson["an"] = item->an;
            itemJson["rating"] = item->rating;
            itemJson["tip"] = item->tip;
            if (!item->artist.empty()) itemJson["artist"] = item->artist;
            if (!item->autor.empty()) itemJson["autor"] = item->autor;
            
            if (utilizatori[username].ratinguriItem.find(id) != utilizatori[username].ratinguriItem.end()) {
                itemJson["userRating"] = utilizatori[username].ratinguriItem[id];
            }
            
            items.push_back(itemJson);
        }
        
        response["success"] = true;
        response["items"] = items;
        response["total"] = items.size();
        return response;
    }

    json acordaRatingAPI(const string& username, int itemId, double rating, const string& context = "normal") {
    json response;
    
    if (utilizatori.find(username) == utilizatori.end()) {
        response["success"] = false;
        response["message"] = "Utilizator inexistent!";
        return response;
    }
    
    Item* item = gasestItemDupaId(itemId);
    if (!item) {
        response["success"] = false;
        response["message"] = "Item inexistent!";
        return response;
    }
    
    if (rating < 1.0 || rating > 10.0) {
        response["success"] = false;
        response["message"] = "Rating invalid! Trebuie sДѓ fie Г®ntre 1 И™i 10.";
        return response;
    }
    
    utilizatori[username].ratinguriItem[itemId] = rating;
    utilizatori[username].itemuriPreferate.insert(itemId);
    
    // Daca ratingul este dat in contextul unei recomandari, salveaza si acest lucru
    if (context == "recommendation") {
        time_t acum = time(0);
        tm* dataLocala = localtime(&acum);
        stringstream dataStr;
        dataStr << (dataLocala->tm_year + 1900) << "-" 
                << setw(2) << setfill('0') << (dataLocala->tm_mon + 1) << "-"
                << setw(2) << setfill('0') << dataLocala->tm_mday;
        
        utilizatori[username].recomandariRatinguite[itemId] = {rating, dataStr.str()};
        utilizatori[username].istoricRecomandari.insert(itemId);
    }
    
    salveazaUtilizatori();
    
    response["success"] = true;
    response["message"] = "Rating acordat cu succes!";
    if (context == "recommendation") {
        response["message"] += " Recomandare evaluatДѓ.";
    }
    return response;
}

json salveazaRatingRecomandareAPI(const string& username, int itemId, double rating, const string& tipRecomandare = "") {
    json response;
    
    if (utilizatori.find(username) == utilizatori.end()) {
        response["success"] = false;
        response["message"] = "Utilizator inexistent!";
        return response;
    }
    
    Item* item = gasestItemDupaId(itemId);
    if (!item) {
        response["success"] = false;
        response["message"] = "Item inexistent!";
        return response;
    }
    
    if (rating < 1.0 || rating > 10.0) {
        response["success"] = false;
        response["message"] = "Rating invalid! Trebuie sДѓ fie Г®ntre 1 И™i 10.";
        return response;
    }
    
    // Salveaza ratingul pentru item
    utilizatori[username].ratinguriItem[itemId] = rating;
    
    // Daca este o recomandare, salveaza si in istoricul de recomandari
    if (!tipRecomandare.empty()) {
        // Obtine data curenta
        time_t acum = time(0);
        tm* dataLocala = localtime(&acum);
        stringstream dataStr;
        dataStr << (dataLocala->tm_year + 1900) << "-" 
                << setw(2) << setfill('0') << (dataLocala->tm_mon + 1) << "-"
                << setw(2) << setfill('0') << dataLocala->tm_mday;
        
        utilizatori[username].recomandariRatinguite[itemId] = {rating, dataStr.str()};
        utilizatori[username].istoricRecomandari.insert(itemId);
    }
    
    salveazaUtilizatori();
    
    response["success"] = true;
    response["message"] = "Rating salvat cu succes!";
    if (!tipRecomandare.empty()) {
        response["message"] += " Recomandare evaluatДѓ.";
    }
    return response;
}

    json getRatinguriAPI(const string& username) {
        json response;
        
        if (utilizatori.find(username) == utilizatori.end()) {
            response["success"] = false;
            response["message"] = "Utilizator inexistent!";
            return response;
        }
        
        json items = json::array();
        vector<pair<int, double>> ratinguriSortate(
            utilizatori[username].ratinguriItem.begin(),
            utilizatori[username].ratinguriItem.end()
        );
        
        sort(ratinguriSortate.begin(), ratinguriSortate.end(),
             [](const pair<int, double>& a, const pair<int, double>& b) {
                 return a.second > b.second;
             });
        
        for (const auto& pereche : ratinguriSortate) {
            Item* item = gasestItemDupaId(pereche.first);
            if (!item) continue;
            
            json itemJson;
            itemJson["id"] = item->id;
            itemJson["titlu"] = item->titlu;
            itemJson["tip"] = item->tip;
            itemJson["rating"] = pereche.second;
            
            items.push_back(itemJson);
        }
        
        response["success"] = true;
        response["items"] = items;
        response["total"] = items.size();
        return response;
    }

    // Functie helper pentru data curenta
string getDataCurenta() {
    time_t acum = time(0);
    tm* dataLocala = localtime(&acum);
    stringstream dataStr;
    dataStr << (dataLocala->tm_year + 1900) << "-" 
            << setw(2) << setfill('0') << (dataLocala->tm_mon + 1) << "-"
            << setw(2) << setfill('0') << dataLocala->tm_mday << " "
            << setw(2) << setfill('0') << dataLocala->tm_hour << ":"
            << setw(2) << setfill('0') << dataLocala->tm_min;
    return dataStr.str();
}

    json genereazaRecomandariAPI(const string& username, const string& tip) {
    json response;

    cout << "DEBUG: genereazaRecomandariAPI - user: " << username 
         << ", tip: " << tip << endl;

    // Verifica daca utilizatorul exista
    if (utilizatori.find(username) == utilizatori.end()) {
        cout << "DEBUG: Utilizator inexistent!" << endl;
        response["success"] = false;
        response["message"] = "Utilizator inexistent!";
        return response;
    }

    // Verifica daca are itemuri preferate
    if (utilizatori[username].itemuriPreferate.empty() &&
    utilizatori[username].ratinguriItem.empty()) {
    // nici favorite, nici ratinguri
    response["success"] = false;
    response["message"] = "Trebuie să adaugi cel puțin un item la favorite sau să acorzi un rating!";
    return response;
}


    // Construim baza de preferințe pentru acest tip: favorite + itemi cu rating
set<int> preferinteByType;

// 1) Favorite de acest tip
for (int id : utilizatori[username].itemuriPreferate) {
    Item* item = gasestItemDupaId(id);
    if (item && item->tip == tip) {
        preferinteByType.insert(id);
    }
}

// 2) Itemi cu rating de acest tip
for (const auto& pr : utilizatori[username].ratinguriItem) {
    int id = pr.first;
    Item* item = gasestItemDupaId(id);
    if (item && item->tip == tip) {
        preferinteByType.insert(id);
    }
}

// Dacă nu avem NIMIC de acest tip, atunci chiar nu putem recomanda
if (preferinteByType.empty()) {
    cout << "DEBUG: Utilizatorul nu are nici favorite, nici ratinguri de tipul: " << tip << endl;
    response["success"] = false;
    response["message"] = "Nu ai favorite sau ratinguri de acest tip!";
    return response;
}


    vector<Recomandare> recomandariFinale;
    map<int, double> scoruriHibride;

    // =========================
    // SISTEM HIBRID: Content-Based + Collaborative Filtering
    // =========================

    // -------------------------
    // Partea 1: Content-Based 
    // -------------------------
    map<int, double> scoruriContent;
    const int k = 5;

    cout << "DEBUG: Incep calculul content-based pentru " 
         << preferinteByType.size() << " preferinte." << endl;

    for (int itemId : preferinteByType) {
        vector<Recomandare> vecini = gasesteCeiMaiApropriatiKVecini(itemId, k, preferinteByType, tip);
        for (const auto& rec : vecini) {
            scoruriContent[rec.itemId] += rec.similaritate;
        }
    }

    for (auto& pereche : scoruriContent) {
        pereche.second /= preferinteByType.size();
        scoruriHibride[pereche.first] += pereche.second * 0.7;
    }

    cout << "DEBUG: scoruriContent.size() = " << scoruriContent.size() << endl;

    // -------------------------
    // Partea 2: Collaborative Filtering 
    // -------------------------
    if (utilizatori[username].ratinguriItem.size() >= 1 || 
    !utilizatori[username].itemuriPreferate.empty()) {
        cout << "DEBUG: Utilizatorul are suficiente ratinguri. Pornesc collaborative filtering." << endl;

        vector<pair<string, double>> utilizatoriSimilari = gasestUtilizatoriSimilari(username, 10);

        if (!utilizatoriSimilari.empty()) {
            map<int, double> scoruriColaborative;
            map<int, int> numarVoturi;

            for (const auto& pereche : utilizatoriSimilari) {
                const Utilizator& userSimilar = utilizatori[pereche.first];
                double similaritate = pereche.second;

                for (const auto& rating : userSimilar.ratinguriItem) {
                    int itemId = rating.first;
                    double ratingItem = rating.second;

                    Item* item = gasestItemDupaId(itemId);
                    if (!item || item->tip != tip) continue;

                     // Sari peste item-urile deja cunoscute de utilizator (favorite sau cu rating)
                    if (utilizatori[username].itemuriPreferate.count(itemId) > 0 ||
    utilizatori[username].ratinguriItem.count(itemId) > 0)
    continue;

                    scoruriColaborative[itemId] += ratingItem * similaritate;
                    numarVoturi[itemId]++;
                }
            }

            cout << "DEBUG: scoruriColaborative.size() = " << scoruriColaborative.size() << endl;

            for (const auto& pereche : scoruriColaborative) {
                if (numarVoturi[pereche.first] > 0) {
                    double scorMediu = pereche.second / numarVoturi[pereche.first];
                    // normalizare pe 10 
                    scoruriHibride[pereche.first] += (scorMediu / 10.0) * 0.3;
                }
            }
        } else {
            cout << "DEBUG: Nu s-au gasit utilizatori similari." << endl;
        }
    } else {
        // Daca utilizatorul nu are suficiente ratinguri, mareste ponderea content-based
        cout << "DEBUG: Utilizatorul NU are suficiente ratinguri. Folosesc doar content-based." << endl;
        scoruriHibride.clear();
        for (auto& pereche : scoruriContent) {
            scoruriHibride[pereche.first] = pereche.second; // practic 100% content-based
        }
    }

    // =========================
    // Convertim scorurile hibride in vector de Recomandare
    // =========================
    for (const auto& pereche : scoruriHibride) {
        recomandariFinale.push_back(Recomandare(pereche.first, pereche.second));
    }

    cout << "DEBUG: recomandariFinale.size() = " << recomandariFinale.size() << endl;

    // Sorteaza recomandarile (heapSort presupus descrescator dupa similaritate)
    heapSort(recomandariFinale);

    if (recomandariFinale.empty()) {
        cout << "DEBUG: Nu s-au gasit recomandari dupa sortare." << endl;
        response["success"] = false;
        response["message"] = "Nu s-au gДѓsit recomandДѓri!";
        return response;
    }

    json items = json::array();
    int displayCount = min(10, (int)recomandariFinale.size());

    // SECtiUNEA DE AFIИsARE + ISTORIC + PREGД‚TIRE PENTRU SALVARE AUTOMATД‚
    vector<int> itemIds; // pentru salveazaRecomandariAPI

    cout << "DEBUG: Afisez si pregatesc pentru salvare " << displayCount << " recomandari." << endl;

    // mergem de la sfarsit la inceput, presupunand ca heapSort pune cele mai mici la inceput
    for (int i = (int)recomandariFinale.size() - 1; 
         i >= (int)recomandariFinale.size() - displayCount && i >= 0; 
         i--) 
    {
        Item* item = gasestItemDupaId(recomandariFinale[i].itemId);
        if (!item) {
            cout << "DEBUG: Item cu id " << recomandariFinale[i].itemId << " nu a fost gasit." << endl;
            continue;
        }

        json itemJson;
        itemJson["id"] = item->id;
        itemJson["titlu"] = item->titlu;
        itemJson["genuri"] = item->genuri;
        itemJson["an"] = item->an;
        itemJson["rating"] = item->rating;
        itemJson["tip"] = item->tip;
        itemJson["matchPercent"] = (int)round(recomandariFinale[i].similaritate * 100);

        if (!item->artist.empty()) itemJson["artist"] = item->artist;
        if (!item->autor.empty())  itemJson["autor"]  = item->autor;

        // Verifica daca utilizatorul a evaluat deja aceasta recomandare
        if (utilizatori[username].recomandariRatinguite.find(item->id) != utilizatori[username].recomandariRatinguite.end()) {
            itemJson["userRating"] = utilizatori[username].recomandariRatinguite[item->id].first;
            itemJson["ratingData"] = utilizatori[username].recomandariRatinguite[item->id].second;
            itemJson["hasRating"] = true;
        } else {
            itemJson["hasRating"] = false;
        }

        items.push_back(itemJson);

        // Adauga la istoric general
        utilizatori[username].istoricRecomandari.insert(item->id);

        // Adauga ID-urile pentru salvare automatДѓ
        itemIds.push_back(item->id);
    }

    // =========================
    // SALVARE AUTOMATa‚ - iNAINTE DE RETURN
    // =========================
    cout << "DEBUG: Incep salvarea automata pentru " << itemIds.size() << " itemi." << endl;

    if (!itemIds.empty()) {
        try {
            json saveResponse = salveazaRecomandariAPI(username, itemIds, "auto-" + tip);

            bool ok = saveResponse.value("success", false);
            string msg = saveResponse.value("message", string(""));

            cout << "DEBUG: Raspuns salveazaRecomandariAPI - success: " << ok 
                 << ", message: " << msg << endl;

            if (!ok) {
                cout << "DEBUG: EROARE la salvare automata de recomandari!" << endl;
            }
        } catch (const exception& e) {
            cout << "DEBUG: EXCEPTIE la salvare automata: " << e.what() << endl;
        }
    } else {
        cout << "DEBUG: Niciun item pentru salvare automata!" << endl;
    }

    // ATENTIE: salveazaUtilizatori() se apeleaza DOAR in salveazaRecomandariAPI,
    // ca sa nu avem salvare dubla.

    // =========================
    // RASPUNS FINAL API
    // =========================
    response["success"] = true;
    response["message"] = "RecomandДѓri generate cu succes!";
    response["items"] = items;
    response["total"] = items.size();

    cout << "DEBUG: genereazaRecomandariAPI - finalizat cu succes, items.size() = " 
         << items.size() << endl;

    return response;
}



 json salveazaRecomandariAPI(const string& username, const vector<int>& itemIds, const string& tipRecomandare) {
    json response;
    
    if (utilizatori.find(username) == utilizatori.end()) {
        response["success"] = false;
        response["message"] = "Utilizator inexistent!";
        return response;
    }
    
    if (itemIds.empty()) {
        response["success"] = false;
        response["message"] = "Nu ai selectat niciun item pentru salvare!";
        return response;
    }
    
    // Generează un timestamp unic pentru această sesiune de recomandări
    time_t acum = time(0);
    string timestamp = to_string(acum);
    
    vector<RecomandareSalvata> recomandariDeSalvat;
    
    for (int itemId : itemIds) {
        Item* item = gasestItemDupaId(itemId);
        if (!item) continue;
        
        RecomandareSalvata rec;
        rec.itemId = itemId;
        rec.titlu = item->titlu;
        rec.genuri = item->genuri;
        rec.an = item->an;
        rec.rating = item->rating;
        rec.tip = item->tip;
        rec.artist = item->artist;
        rec.autor = item->autor;
        rec.dataGenerare = getDataCurenta();
        rec.tipRecomandare = tipRecomandare;
        rec.evaluata = false;
        rec.ratingUtilizator = 0.0;
        rec.dataEvaluare = "";
        
        // Încearcă să găsești similaritatea din recomandările recente
        rec.similaritate = 0.0; // Valoare implicită
        
        // Verifică dacă utilizatorul are deja un rating pentru acest item
        auto it = utilizatori[username].ratinguriItem.find(itemId);
        if (it != utilizatori[username].ratinguriItem.end()) {
            rec.evaluata = true;
            rec.ratingUtilizator = it->second;
            rec.dataEvaluare = getDataCurenta();
        }
        
        recomandariDeSalvat.push_back(rec);
        
        // Adaugă și în istoricul general
        utilizatori[username].istoricRecomandari.insert(itemId);
    }
    
    if (!recomandariDeSalvat.empty()) {
        utilizatori[username].recomandariSalvate[timestamp] = recomandariDeSalvat;
        salveazaUtilizatori();
        
        response["success"] = true;
        response["message"] = "Recomandări salvate cu succes! (" + to_string(recomandariDeSalvat.size()) + " itemi)";
        response["timestamp"] = timestamp;
    } else {
        response["success"] = false;
        response["message"] = "Nu s-au putut salva recomandările!";
    }
    
    return response;
}


json getRecomandariSalvateAPI(const string& username) {
    json response;
    
    if (utilizatori.find(username) == utilizatori.end()) {
        response["success"] = false;
        response["message"] = "Utilizator inexistent!";
        return response;
    }
    
    const auto& recomandari = utilizatori[username].recomandariSalvate;
    
    if (recomandari.empty()) {
        response["success"] = false;
        response["message"] = "Nu ai recomandări salvate!";
        return response;
    }
    
    json sesiuniJson = json::array();
    
    for (const auto& sesiune : recomandari) {
        json sesiuneJson;
        sesiuneJson["timestamp"] = sesiune.first;
        sesiuneJson["dataGenerare"] = !sesiune.second.empty() ? sesiune.second[0].dataGenerare : "";
        sesiuneJson["numarItemi"] = sesiune.second.size();
        
        json itemsJson = json::array();
        for (const auto& rec : sesiune.second) {
            // ⭐⭐ ACUM FOLOSIM DATELE SALVATE DIRECT, NU MAI CAUTĂM ÎN BAZA DE DATE ⭐⭐
            json itemJson;
            itemJson["id"] = rec.itemId;
            itemJson["titlu"] = rec.titlu;
            itemJson["genuri"] = rec.genuri;
            itemJson["an"] = rec.an;
            itemJson["rating"] = rec.rating;
            itemJson["tip"] = rec.tip;
            itemJson["similaritate"] = rec.similaritate;
            itemJson["dataGenerare"] = rec.dataGenerare;
            itemJson["tipRecomandare"] = rec.tipRecomandare;
            itemJson["evaluata"] = rec.evaluata;
            itemJson["ratingUtilizator"] = rec.ratingUtilizator;
            itemJson["dataEvaluare"] = rec.dataEvaluare;
            
            if (!rec.artist.empty()) itemJson["artist"] = rec.artist;
            if (!rec.autor.empty()) itemJson["autor"] = rec.autor;
            
            itemsJson.push_back(itemJson);
        }
        
        sesiuneJson["items"] = itemsJson;
        sesiuniJson.push_back(sesiuneJson);
    }
    
    response["success"] = true;
    response["sesiuni"] = sesiuniJson;
    response["totalSesiuni"] = sesiuniJson.size();
    return response;
}


        
        

    json getIstoricRecomandariAPI(const string& username) {
    json response;
    
    if (utilizatori.find(username) == utilizatori.end()) {
        response["success"] = false;
        response["message"] = "Utilizator inexistent!";
        return response;
    }
    
    json items = json::array();
    const auto& user = utilizatori[username];
    
    // Creeaza o lista de recomandari ratinguite pentru a le afisa primele
    vector<pair<Item, pair<double, string>>> recomandariRatinguite;
    vector<Item> recomandariFaraRating;
    
    for (int itemId : user.istoricRecomandari) {
        Item* item = gasestItemDupaId(itemId);
        if (!item) continue;
        
        auto it = user.recomandariRatinguite.find(itemId);
        if (it != user.recomandariRatinguite.end()) {
            // Are rating
            recomandariRatinguite.push_back({*item, it->second});
        } else {
            // Nu are rating
            recomandariFaraRating.push_back(*item);
        }
    }
    
    // Sorteaza recomandarile ratinguite dupa data (cele mai recente primele)
    sort(recomandariRatinguite.begin(), recomandariRatinguite.end(),
         [](const pair<Item, pair<double, string>>& a, const pair<Item, pair<double, string>>& b) {
             return a.second.second > b.second.second; // Compara datele
         });
    
    // Adauga recomandДѓrile ratinguite
    for (const auto& pereche : recomandariRatinguite) {
        const Item& item = pereche.first;
        double rating = pereche.second.first;
        string data = pereche.second.second;
        
        json itemJson;
        itemJson["id"] = item.id;
        itemJson["titlu"] = item.titlu;
        itemJson["genuri"] = item.genuri;
        itemJson["an"] = item.an;
        itemJson["rating"] = item.rating;
        itemJson["tip"] = item.tip;
        itemJson["userRating"] = rating;
        itemJson["ratingData"] = data;
        itemJson["hasRating"] = true;
        if (!item.artist.empty()) itemJson["artist"] = item.artist;
        if (!item.autor.empty()) itemJson["autor"] = item.autor;
        
        items.push_back(itemJson);
    }
    
    // Adauga recomandarile fara rating
    for (const Item& item : recomandariFaraRating) {
        json itemJson;
        itemJson["id"] = item.id;
        itemJson["titlu"] = item.titlu;
        itemJson["genuri"] = item.genuri;
        itemJson["an"] = item.an;
        itemJson["rating"] = item.rating;
        itemJson["tip"] = item.tip;
        itemJson["hasRating"] = false;
        if (!item.artist.empty()) itemJson["artist"] = item.artist;
        if (!item.autor.empty()) itemJson["autor"] = item.autor;
        
        items.push_back(itemJson);
    }
    
    response["success"] = true;
    response["items"] = items;
    response["total"] = items.size();
    response["withRating"] = recomandariRatinguite.size();
    response["withoutRating"] = recomandariFaraRating.size();
    return response;
}

    json getGenuriDisponibileAPI() {
        json response;
        vector<string> genuriList(toateGenurile.begin(), toateGenurile.end());
        sort(genuriList.begin(), genuriList.end());
        
        response["success"] = true;
        response["genuri"] = genuriList;
        response["total"] = genuriList.size();
        return response;
    }


    // Functie pentru obtinerea topului item-ilor pe baza ratingurilor utilizatorilor
    json getTopItemiUtilizatoriAPI(const string& tip = "", int limit = 10) {
        json response;
        
        // Calculeaza ratingurile medii pentru toate item-ile
        map<int, pair<double, int>> ratinguriMedii = calculeazaRatinguriMedii();
        
        vector<ItemRating> topItemi;
        
        for (const auto& pereche : ratinguriMedii) {
            int itemId = pereche.first;
            double sumaRatinguri = pereche.second.first;
            int numarRatinguri = pereche.second.second;
            double ratingMediu = sumaRatinguri / numarRatinguri;
            
            Item* item = gasestItemDupaId(itemId);
            if (!item) continue;
            
            // Filtreaza dupa tip daca este specificat
            if (!tip.empty() && item->tip != tip) continue;
            
            topItemi.push_back(ItemRating(itemId, ratingMediu, numarRatinguri, item->titlu, item->tip));
        }
        
        // Verifica daca exista itemi cu ratinguri
        if (topItemi.empty()) {
            response["success"] = false;
            response["message"] = "Nu existДѓ suficiente ratinguri de la utilizatori pentru a genera un top!";
            return response;
        }
        
        // Sorteaza descrescator dupa rating mediu
        sort(topItemi.begin(), topItemi.end(),
             [](const ItemRating& a, const ItemRating& b) {
                 if (abs(a.ratingMediu - b.ratingMediu) < 0.01) {
                     // Daca ratingurile sunt egale, sorteaza dupa numarul de ratinguri
                     return a.numarRatinguri > b.numarRatinguri;
                 }
                 return a.ratingMediu > b.ratingMediu;
             });
        
        json items = json::array();
        int count = min(limit, (int)topItemi.size());
        
        for (int i = 0; i < count; i++) {
            Item* item = gasestItemDupaId(topItemi[i].itemId);
            if (!item) continue;
            
            json itemJson;
            itemJson["id"] = item->id;
            itemJson["titlu"] = item->titlu;
            itemJson["genuri"] = item->genuri;
            itemJson["an"] = item->an;
            itemJson["tip"] = item->tip;
            itemJson["ratingMediu"] = round(topItemi[i].ratingMediu * 10) / 10.0;
            itemJson["numarRatinguri"] = topItemi[i].numarRatinguri;
            itemJson["pozitie"] = i + 1;
            if (!item->artist.empty()) itemJson["artist"] = item->artist;
            if (!item->autor.empty()) itemJson["autor"] = item->autor;
            
            items.push_back(itemJson);
        }
        
        response["success"] = true;
        response["items"] = items;
        response["total"] = items.size();
        response["tip"] = tip.empty() ? "toate" : tip;
        response["sursa"] = "utilizatori";
        return response;
    }

    // Functie pentru obtinerea topului item-ilor pe baza ratingurilor din baza de date
    json getTopItemiBazaDateAPI(const string& tip = "", int limit = 10) {
        json response;
        
        vector<pair<Item, double>> itemiCuRating;
        
        for (const auto& item : bazaDate) {
            // Filtreaza dupa tip daca este specificat
            if (!tip.empty() && item.tip != tip) continue;
            
            itemiCuRating.push_back({item, item.rating});
        }
        
        // Verifica daca exista itemi
        if (itemiCuRating.empty()) {
            response["success"] = false;
            response["message"] = "Nu existДѓ itemi Г®n baza de date!";
            return response;
        }
        
        // Sorteaza descrescator dupДѓ rating
        sort(itemiCuRating.begin(), itemiCuRating.end(),
             [](const pair<Item, double>& a, const pair<Item, double>& b) {
                 return a.second > b.second;
             });
        
        json items = json::array();
        int count = min(limit, (int)itemiCuRating.size());
        
        for (int i = 0; i < count; i++) {
            const Item& item = itemiCuRating[i].first;
            
            json itemJson;
            itemJson["id"] = item.id;
            itemJson["titlu"] = item.titlu;
            itemJson["genuri"] = item.genuri;
            itemJson["an"] = item.an;
            itemJson["tip"] = item.tip;
            itemJson["rating"] = item.rating;
            itemJson["pozitie"] = i + 1;
            if (!item.artist.empty()) itemJson["artist"] = item.artist;
            if (!item.autor.empty()) itemJson["autor"] = item.autor;
            
            items.push_back(itemJson);
        }
        
        response["success"] = true;
        response["items"] = items;
        response["total"] = items.size();
        response["tip"] = tip.empty() ? "toate" : tip;
        response["sursa"] = "baza_date";
        return response;
    }

    // Functie pentru obtinerea item-ilor din anul cel mai recent
    json getItemiRecentiAPI(int limit = 10) {
        json response;
        
        // Gaseste anul cel mai recent
        int anMaxim = 0;
        for (const auto& item : bazaDate) {
            if (item.an > anMaxim) {
                anMaxim = item.an;
            }
        }
        
        // Verifica dac s-a gasit vreun an
        if (anMaxim == 0) {
            response["success"] = false;
            response["message"] = "Nu existДѓ itemi Г®n baza de date!";
            return response;
        }
        
        vector<Item> itemiRecenti;
        
        for (const auto& item : bazaDate) {
            if (item.an == anMaxim) {
                itemiRecenti.push_back(item);
            }
        }
        
        // Verifica daca exista itemi din anul recent
        if (itemiRecenti.empty()) {
            response["success"] = false;
            response["message"] = "Nu existДѓ itemi din anul " + to_string(anMaxim) + "!";
            return response;
        }
        
        // Sorteaza descrescator dupa rating
        sort(itemiRecenti.begin(), itemiRecenti.end(),
             [](const Item& a, const Item& b) {
                 return a.rating > b.rating;
             });
        
        json items = json::array();
        int count = min(limit, (int)itemiRecenti.size());
        
        for (int i = 0; i < count; i++) {
            const Item& item = itemiRecenti[i];
            
            json itemJson;
            itemJson["id"] = item.id;
            itemJson["titlu"] = item.titlu;
            itemJson["genuri"] = item.genuri;
            itemJson["an"] = item.an;
            itemJson["rating"] = item.rating;
            itemJson["tip"] = item.tip;
            itemJson["pozitie"] = i + 1;
            if (!item.artist.empty()) itemJson["artist"] = item.artist;
            if (!item.autor.empty()) itemJson["autor"] = item.autor;
            
            items.push_back(itemJson);
        }
        
        response["success"] = true;
        response["items"] = items;
        response["total"] = items.size();
        response["an"] = anMaxim;
        return response;
    }

    // Functie pentru obtinerea tuturor topurilor pentru o categorie specifica
    json getToateTopurilePentruCategorieAPI(const string& tip) {
        json response;
        
        if (tip != "film" && tip != "carte" && tip != "muzica") {
            response["success"] = false;
            response["message"] = "Tip invalid! Trebuie sДѓ fie 'film', 'carte' sau 'muzica'.";
            return response;
        }
        
        // Obtine topul din baza de date
        json topBazaDate = getTopItemiBazaDateAPI(tip, 10);
        json topUtilizatori = getTopItemiUtilizatoriAPI(tip, 10);
        
        response["success"] = true;
        response["tip"] = tip;
        response["numeTip"] = getNumeTip(tip);
        response["iconita"] = getIconitaTip(tip);
        
        if (topBazaDate["success"]) {
            response["topBazaDate"] = topBazaDate["items"];
        } else {
            response["topBazaDate"] = json::array();
        }
        
        if (topUtilizatori["success"]) {
            response["topUtilizatori"] = topUtilizatori["items"];
        } else {
            response["topUtilizatori"] = json::array();
        }
        
        return response;
    }

    // Functii helper pentru nume si iconite
    string getNumeTip(const string& tip) {
        if (tip == "film") return "Filme";
        if (tip == "carte") return "CДѓrИ›i";
        if (tip == "muzica") return "MuzicДѓ";
        return "Necunoscut";
    }

    string getIconitaTip(const string& tip) {
        if (tip == "film") return "рџЋ¬";
        if (tip == "carte") return "рџ“љ";
        if (tip == "muzica") return "рџЋµ";
        return "рџ“Ѓ";
    }
};

int main() {
    SetConsoleOutputCP(CP_UTF8);
    
    SistemRecomandare sistem;
    crow::SimpleApp app;

    // Serveste fisierul HTML
    CROW_ROUTE(app, "/")
    ([]() {
        ifstream file("frontend.html");
        if (!file.is_open()) {
            return crow::response(404, "Frontend nu a fost gДѓsit!");
        }
        
        stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        
        auto res = crow::response(buffer.str());
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    // API: inregistrare
    CROW_ROUTE(app, "/api/register").methods("POST"_method)
    ([&sistem](const crow::request& req) {
        auto data = json::parse(req.body);
        string username = data["username"];
        string password = data["password"];
        
        json response = sistem.inregistrareUtilizatorAPI(username, password);
        return crow::response(response.dump());
    });

    // API: Autentificare
    CROW_ROUTE(app, "/api/login").methods("POST"_method)
    ([&sistem](const crow::request& req) {
        auto data = json::parse(req.body);
        string username = data["username"];
        string password = data["password"];
        
        json response = sistem.autentificareUtilizatorAPI(username, password);
        return crow::response(response.dump());
    });

    // API: Obtine baza de date
    CROW_ROUTE(app, "/api/database")
    ([&sistem](const crow::request& req) {
        string tip = req.url_params.get("tip") ? req.url_params.get("tip") : "";
        json response = sistem.getBazaDateAPI(tip);
        return crow::response(response.dump());
    });

    // API: Cauta in baza de date
    CROW_ROUTE(app, "/api/search")
    ([&sistem](const crow::request& req) {
        string query = req.url_params.get("q") ? req.url_params.get("q") : "";
        json response = sistem.cautaInBazaDateAPI(query);
        return crow::response(response.dump());
    });

        // API: Profil debug al utilizatorului (preferințe pe genuri)
    CROW_ROUTE(app, "/api/profile/debug")
    ([&sistem](const crow::request& req) {
        string username = req.url_params.get("username") ? req.url_params.get("username") : "";
        
        json response = sistem.getProfilUtilizatorDebugAPI(username);
        return crow::response(response.dump());
    });


    // API: Adauga la favorite
    CROW_ROUTE(app, "/api/favorites/add").methods("POST"_method)
    ([&sistem](const crow::request& req) {
        auto data = json::parse(req.body);
        string username = data["username"];
        int itemId = data["itemId"];
        
        json response = sistem.adaugaLaFavoriteAPI(username, itemId);
        return crow::response(response.dump());
    });

    // API: Elimina din favorite
CROW_ROUTE(app, "/api/favorites/remove").methods("POST"_method)
([&sistem](const crow::request& req) {
    auto data = json::parse(req.body);
    string username = data["username"];
    int itemId = data["itemId"];
    
    json response = sistem.eliminaDinFavoriteAPI(username, itemId);
    return crow::response(response.dump());
});

    // API: Obtine favorite
    CROW_ROUTE(app, "/api/favorites")
    ([&sistem](const crow::request& req) {
        string username = req.url_params.get("username") ? req.url_params.get("username") : "";
        json response = sistem.getFavoriteAPI(username);
        return crow::response(response.dump());
    });

    // API: Acorda rating
    CROW_ROUTE(app, "/api/rating").methods("POST"_method)
    ([&sistem](const crow::request& req) {
        auto data = json::parse(req.body);
        string username = data["username"];
        int itemId = data["itemId"];
        double rating = data["rating"];
        
        json response = sistem.acordaRatingAPI(username, itemId, rating);
        return crow::response(response.dump());
    });

    // API: Obtine ratinguri
    CROW_ROUTE(app, "/api/ratings")
    ([&sistem](const crow::request& req) {
        string username = req.url_params.get("username") ? req.url_params.get("username") : "";
        json response = sistem.getRatinguriAPI(username);
        return crow::response(response.dump());
    });

    // API: Genereaza recomandДѓri
    CROW_ROUTE(app, "/api/recommendations")
    ([&sistem](const crow::request& req) {
        string username = req.url_params.get("username") ? req.url_params.get("username") : "";
        string tip = req.url_params.get("tip") ? req.url_params.get("tip") : "film";
        
        json response = sistem.genereazaRecomandariAPI(username, tip);
        return crow::response(response.dump());
    });

    

// API: Obtine recomandДѓri salvate
CROW_ROUTE(app, "/api/recommendations/saved")
([&sistem](const crow::request& req) {
    string username = req.url_params.get("username") ? req.url_params.get("username") : "";
    
    json response = sistem.getRecomandariSalvateAPI(username);
    return crow::response(response.dump());
});

    // API: Salveaza rating pentru recomandare
CROW_ROUTE(app, "/api/rating/recommendation").methods("POST"_method)
([&sistem](const crow::request& req) {
    auto data = json::parse(req.body);
    string username = data["username"];
    int itemId = data["itemId"];
    double rating = data["rating"];
    string tipRecomandare = data.contains("tipRecomandare") ? string(data["tipRecomandare"]) : "hibrid";
    
    json response = sistem.salveazaRatingRecomandareAPI(username, itemId, rating, tipRecomandare);
    return crow::response(response.dump());
});

// API: Obtineistoric recomandДѓri
CROW_ROUTE(app, "/api/recommendations/history")
([&sistem](const crow::request& req) {
    string username = req.url_params.get("username") ? req.url_params.get("username") : "";
    
    json response = sistem.getIstoricRecomandariAPI(username);
    return crow::response(response.dump());
});

    // API: Obtine genurile disponibile
    CROW_ROUTE(app, "/api/genres")
    ([&sistem]() {
        json response = sistem.getGenuriDisponibileAPI();
        return crow::response(response.dump());
    });

   

    // API: Obtine topul item-ilor pe baza ratingurilor utilizatorilor
    CROW_ROUTE(app, "/api/top-users")
    ([&sistem](const crow::request& req) {
        string tip = req.url_params.get("tip") ? req.url_params.get("tip") : "";
        int limit = req.url_params.get("limit") ? atoi(req.url_params.get("limit")) : 10;
        
        json response = sistem.getTopItemiUtilizatoriAPI(tip, limit);
        return crow::response(response.dump());
    });

    // API: Obtine topul item-ilor pe baza ratingurilor din baza de date
    CROW_ROUTE(app, "/api/top-database")
    ([&sistem](const crow::request& req) {
        string tip = req.url_params.get("tip") ? req.url_params.get("tip") : "";
        int limit = req.url_params.get("limit") ? atoi(req.url_params.get("limit")) : 10;
        
        json response = sistem.getTopItemiBazaDateAPI(tip, limit);
        return crow::response(response.dump());
    });

    // API: Obtine toate topurile pentru o categorie specifica
    CROW_ROUTE(app, "/api/top-categorie")
    ([&sistem](const crow::request& req) {
        string tip = req.url_params.get("tip") ? req.url_params.get("tip") : "film";
        
        json response = sistem.getToateTopurilePentruCategorieAPI(tip);
        return crow::response(response.dump());
    });

    // API: Obtine item-ii din anul cel mai recent
    CROW_ROUTE(app, "/api/recent")
    ([&sistem](const crow::request& req) {
        int limit = req.url_params.get("limit") ? atoi(req.url_params.get("limit")) : 10;
        
        json response = sistem.getItemiRecentiAPI(limit);
        return crow::response(response.dump());
    });

    cout << " Serverul PickForYou porneste pe http://localhost:8080\n";
    cout << " Deschide browser-ul si acceseazДѓ: http://localhost:8080\n";
    cout << " Apasa Ctrl+C pentru a opri serverul\n\n";

    app.port(8080).multithreaded().run();
    
    return 0;
}