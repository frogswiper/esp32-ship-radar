#pragma once
// Harbours, ferry terminals and lighthouses shown on the radar (Oslofjord + southern Scandinavia).
struct Port { const char* name; float lat, lon; };
static const Port PORTS[] = {
    {"Oslo",        59.9067f, 10.7380f}, {"Drammen",    59.7389f, 10.2280f}, {"Horten",     59.4178f, 10.4850f},
    {"Moss",        59.4350f, 10.6560f}, {"Tonsberg",   59.2690f, 10.4100f}, {"Sandefjord", 59.1290f, 10.2250f},
    {"Larvik",      59.0450f, 10.0350f}, {"Fredrikstad",59.2100f, 10.9450f}, {"Halden",     59.1220f, 11.3870f},
    {"Holmestrand", 59.4890f, 10.3150f}, {"Drobak",     59.6630f, 10.6290f}, {"Faerder",    59.0270f, 10.5250f},
    {"Langesund",   59.0010f,  9.7500f}, {"Kragero",    58.8690f,  9.4120f}, {"Risor",      58.7220f,  9.2340f},
    {"Arendal",     58.4610f,  8.7720f}, {"Kristiansand",58.1440f, 7.9950f}, {"Stromstad",  58.9380f, 11.1720f},
    {"Goteborg",    57.6950f, 11.9050f}, {"Frederikshavn",57.4360f,10.5460f}, {"Hirtshals",  57.5900f,  9.9630f},
    {"Skagen",      57.7200f, 10.5900f}, {"Lysekil",    58.2740f, 11.4300f}, {"Uddevalla",  58.3450f, 11.9100f},
    {"Stavanger",   58.9730f,  5.7300f}, {"Haugesund",  59.4120f,  5.2680f}, {"Bergen",     60.3970f,  5.3200f},
    {"Egersund",    58.4500f,  6.0000f}, {"Mandal",     58.0270f,  7.4560f}, {"Grimstad",   58.3410f,  8.5930f},
    // northern Norway (Hurtigruten ports, fishing harbours, terminals)
    {"Hammerfest",  70.6634f, 23.6821f}, {"Melkoya LNG", 70.6880f, 23.6080f}, {"Havoysund",   70.9958f, 24.6627f},
    {"Honningsvag", 70.9821f, 25.9704f}, {"Kjollefjord", 70.9450f, 27.3450f}, {"Mehamn",      71.0376f, 27.8503f},
    {"Berlevag",    70.8580f, 29.0869f}, {"Batsfjord",   70.6341f, 29.7194f}, {"Vardo",       70.3705f, 31.1107f},
    {"Vadso",       70.0743f, 29.7490f}, {"Kirkenes",    69.7271f, 30.0450f}, {"Alta",        69.9689f, 23.2716f},
    {"Oksfjord",    70.2400f, 22.3500f}, {"Skjervoy",    70.0349f, 20.9739f}, {"Tromso",      69.6496f, 18.9560f},
    {"Finnsnes",    69.2297f, 17.9810f}, {"Harstad",     68.7983f, 16.5416f}, {"Risoyhamn",   68.9700f, 15.6300f},
    {"Sortland",    68.6950f, 15.4140f}, {"Stokmarknes", 68.5649f, 14.9100f}, {"Svolvaer",    68.2343f, 14.5681f},
    {"Leknes",      68.1470f, 13.6110f}, {"Narvik",      68.4385f, 17.4272f}, {"Bodo",        67.2804f, 14.4049f},
    {"Mo i Rana",   66.3128f, 14.1424f}, {"Sandnessjoen",66.0217f, 12.6318f}, {"Bronnoysund", 65.4738f, 12.2123f},
    {"Rorvik",      64.8622f, 11.2383f}, {"Namsos",      64.4658f, 11.4948f}, {"Trondheim",   63.4378f, 10.3983f},
    {"Kristiansund",63.1104f,  7.7298f}, {"Molde",       62.7372f,  7.1607f}, {"Alesund",     62.4722f,  6.1549f},
    {"Maloy",       61.9357f,  5.1134f}, {"Floro",       61.5996f,  5.0328f},
};
#define PORT_COUNT (sizeof(PORTS)/sizeof(PORTS[0]))
