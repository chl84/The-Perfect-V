#include "nationality.h"
#include <algorithm>
#include <cctype>
#include <random>
#include <stdexcept>

namespace sjp {
namespace {
std::string normalized(std::string_view value) {
    std::string result;
    for (unsigned char c : value)
        if (std::isalnum(c))
            result += char(std::toupper(c));
    return result;
}
struct Names {
    const char *code;
    std::array<const char *, 6> first, last;
};
// Fictional combinations of established given names and surnames. ASCII
// transliterations match the game's existing font and original replay format.
constexpr Names names[] = {
    {"FIN",
     {"Mikko", "Ville", "Juho", "Antti", "Joonas", "Lauri"},
     {"Laine", "Nieminen", "Virtanen", "Korhonen", "Lehtinen", "Salonen"}},
    {"SUI",
     {"Lukas", "Simon", "Fabian", "Marco", "Daniel", "Andreas"},
     {"Keller", "Meier", "Steiner", "Fischer", "Weber", "Baumann"}},
    {"CZE",
     {"Jan", "Petr", "Lukas", "Tomas", "Martin", "Jakub"},
     {"Novak", "Svoboda", "Dvorak", "Cerny", "Prochazka", "Kucera"}},
    {"BLR",
     {"Maksim", "Andrei", "Aliaksandr", "Dzmitry", "Pavel", "Siarhei"},
     {"Kavalenka", "Ivanou", "Kazlou", "Novik", "Piatrou", "Hryshkevich"}},
    {"AUT",
     {"Lukas", "Florian", "Matthias", "Stefan", "Thomas", "Andreas"},
     {"Gruber", "Huber", "Bauer", "Wagner", "Pichler", "Steiner"}},
    {"USA",
     {"James", "William", "Ethan", "Andrew", "Ryan", "Daniel"},
     {"Smith", "Johnson", "Miller", "Wilson", "Brown", "Anderson"}},
    {"LAT",
     {"Janis", "Andris", "Martins", "Kristaps", "Edgars", "Rihards"},
     {"Berzins", "Kalnins", "Ozols", "Liepa", "Liepins", "Krumins"}},
    {"POL",
     {"Piotr", "Jan", "Pawel", "Jakub", "Tomasz", "Michal"},
     {"Kowalski", "Nowak", "Wisniewski", "Wojcik", "Kaminski", "Zielinski"}},
    {"JPN",
     {"Haruto", "Yuki", "Kenta", "Daiki", "Takumi", "Sota"},
     {"Sato", "Suzuki", "Takahashi", "Tanaka", "Watanabe", "Ito"}},
    {"BEL",
     {"Thomas", "Lucas", "Louis", "Arthur", "Simon", "Victor"},
     {"Peeters", "Janssens", "Dubois", "Lambert", "Jacobs", "Martin"}},
    {"ISL",
     {"Aron", "Jon", "Einar", "Gunnar", "Stefan", "Kristjan"},
     {"Jonsson", "Einarsson", "Gunnarsson", "Stefansson", "Arnarsson", "Sigurdsson"}},
    {"ENG",
     {"Oliver", "Harry", "George", "Jack", "Thomas", "William"},
     {"Smith", "Jones", "Taylor", "Brown", "Wilson", "Davies"}},
    {"GER",
     {"Lukas", "Felix", "Jonas", "Paul", "Leon", "Simon"},
     {"Mueller", "Schmidt", "Schneider", "Fischer", "Weber", "Hoffmann"}},
    {"EST",
     {"Martin", "Rasmus", "Karl", "Markus", "Siim", "Tanel"},
     {"Tamm", "Saar", "Sepp", "Kask", "Ilves", "Rebane"}},
    {"NOR",
     {"Anders", "Jonas", "Magnus", "Erik", "Henrik", "Lars"},
     {"Hansen", "Johansen", "Olsen", "Larsen", "Berg", "Nilsen"}},
    {"AUS",
     {"Jack", "William", "Oliver", "Thomas", "James", "Lachlan"},
     {"Smith", "Jones", "Williams", "Brown", "Wilson", "Taylor"}},
    {"IRL",
     {"Sean", "Liam", "Cian", "Conor", "Ronan", "Eoin"},
     {"Murphy", "Kelly", "Byrne", "Walsh", "Ryan", "Doyle"}},
    {"UKR",
     {"Andriy", "Oleksandr", "Dmytro", "Maksym", "Bohdan", "Taras"},
     {"Kovalenko", "Shevchenko", "Bondarenko", "Melnyk", "Tkachenko", "Kovalchuk"}},
    {"HUN",
     {"Bence", "Mate", "Balazs", "Adam", "Gabor", "Daniel"},
     {"Nagy", "Kovacs", "Toth", "Szabo", "Varga", "Kiss"}},
    {"SWE",
     {"Erik", "Johan", "Oskar", "Viktor", "Emil", "Gustav"},
     {"Andersson", "Johansson", "Karlsson", "Nilsson", "Eriksson", "Lindberg"}},
    {"ITA",
     {"Luca", "Marco", "Andrea", "Matteo", "Davide", "Alessandro"},
     {"Rossi", "Bianchi", "Romano", "Ricci", "Conti", "Rinaldi"}},
    {"DEN",
     {"Mikkel", "Magnus", "Frederik", "Emil", "Rasmus", "Jeppe"},
     {"Jensen", "Nielsen", "Hansen", "Pedersen", "Andersen", "Christensen"}},
    {"SVK",
     {"Martin", "Jakub", "Tomas", "Michal", "Peter", "Samuel"},
     {"Horvath", "Kovac", "Varga", "Toth", "Nagy", "Novak"}},
    {"CAN",
     {"Liam", "William", "Noah", "Thomas", "Ethan", "Gabriel"},
     {"Smith", "Brown", "Wilson", "Tremblay", "Gagnon", "Roy"}},
    {"LTU",
     {"Jonas", "Mantas", "Lukas", "Tomas", "Paulius", "Darius"},
     {"Kazlauskas", "Petrauskas", "Jankauskas", "Stankevicius", "Vasiliauskas", "Zukauskas"}},
    {"KAZ",
     {"Nurlan", "Daniyar", "Arman", "Alikhan", "Yerlan", "Timur"},
     {"Saparov", "Omarov", "Ibrayev", "Nurgaliyev", "Akhmetov", "Iskakov"}},
    {"CHN", {"Wei", "Jun", "Tao", "Jie", "Ming", "Hao"}, {"Wang", "Li", "Zhang", "Liu", "Chen", "Yang"}},
    {"FRA",
     {"Louis", "Hugo", "Jules", "Lucas", "Antoine", "Maxime"},
     {"Martin", "Bernard", "Dubois", "Thomas", "Robert", "Moreau"}},
    {"NED",
     {"Daan", "Bram", "Lars", "Jeroen", "Sven", "Pieter"},
     {"de Jong", "Jansen", "de Vries", "Bakker", "Visser", "Smit"}},
    {"RUS",
     {"Ivan", "Dmitry", "Alexei", "Nikolai", "Mikhail", "Sergei"},
     {"Ivanov", "Petrov", "Smirnov", "Volkov", "Popov", "Kuznetsov"}},
    {"KOR",
     {"Min-jun", "Ji-ho", "Seo-jun", "Hyun-woo", "Do-yun", "Jun-seo"},
     {"Kim", "Lee", "Park", "Choi", "Jung", "Kang"}},
    {"SLO",
     {"Luka", "Miha", "Rok", "Matej", "Anze", "Jure"},
     {"Novak", "Horvat", "Kovac", "Krajnc", "Zupan", "Kos"}},
    {"GBR",
     {"Oliver", "Harry", "George", "Jack", "Thomas", "William"},
     {"Smith", "Jones", "Taylor", "Brown", "Wilson", "Davies"}},
};
} // namespace
const Nationality *nationality(std::string_view country) {
    auto key = normalized(country);
    if (key.empty())
        return nullptr;
    if (key == "HOLLAND" || key == "HOL" || key == "NLD")
        key = "NED";
    else if (key == "KOREA" || key == "REPUBLICOFKOREA")
        key = "KOR";
    else if (key == "CZECHIA" || key == "CZECHREPUBLIC")
        key = "CZE";
    else if (key == "DEU")
        key = "GER";
    else if (key == "CHE")
        key = "SUI";
    else if (key == "LVA")
        key = "LAT";
    else if (key == "DNK")
        key = "DEN";
    else if (key == "SVN")
        key = "SLO";
    else if (key == "UK")
        key = "GBR";
    for (const auto &n : NATIONALITIES)
        if (key == n.code || key == normalized(n.name) || key == normalized(n.iso))
            return &n;
    return nullptr;
}
uint32_t athleteIdentitySeed() {
    return std::random_device{}();
}
AthleteIdentity randomAthleteIdentity(uint32_t seed, std::string_view country) {
    std::mt19937 random(seed);
    const auto *nation = nationality(country);
    if (!nation && !country.empty()) throw std::invalid_argument("Unknown athlete nationality");
    if (!nation)
        nation = &NATIONALITIES[std::uniform_int_distribution<size_t>(0, NATIONALITIES.size() - 1)(random)];
    const auto pool = std::find_if(std::begin(names), std::end(names),
                                   [&](const Names &n) { return n.code == std::string_view(nation->code); });
    std::uniform_int_distribution<size_t> choice(0, 5);
    const auto first = pool->first[choice(random)], last = pool->last[choice(random)];
    return {std::string(first) + " " + last, nation->code};
}
} // namespace sjp
