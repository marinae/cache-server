#include "parser.h"

//+----------------------------------------------------------------------------+
//| Cut spaces in front and in the end of string                               |
//+----------------------------------------------------------------------------+

std::string CParser::cutSpaces(std::string line) {

    int space = line.find(' ');

    while (line.size() > 0 && space != std::string::npos) {
        line.erase(space, 1);
        space = line.find(' ');
    }

    return line;
}

//+----------------------------------------------------------------------------+
//| Parse line                                                                 |
//+----------------------------------------------------------------------------+

int CParser::parseLine(std::string line, std::string *key,
                       std::string *value, int *ttl) {

    std::vector<std::string> params;

    while (line.size() > 0) {
        int space = line.find(' ');

        if (space == std::string::npos) {
            params.push_back(line);
            line = "";
        } else {
            std::string tmp = CParser::cutSpaces(line.substr(0, space));
            params.push_back(tmp);
            line = line.substr(space + 1);
        }
    }

    std::vector<std::string> cleanParams;

    for (int i = 0; i < params.size(); ++i) {
        if (params[i].size() > 0)
            cleanParams.push_back(params[i]);
    }

    if (cleanParams[0] == "get") {
        if (cleanParams.size() != 2)
            return 1;
        *key   = cleanParams[1];
        *value = "";
        *ttl   = 0;

    } else if (cleanParams[0] == "set") {
        if (cleanParams.size() != 4)
            return 1;
        *key   = cleanParams[2];
        *value = cleanParams[3];
        try {
            *ttl   = std::stoi(cleanParams[1]);
        } catch(...) {
            *key = "";
            *value = "";
            *ttl   = 0;
            return 1;
        }

    } else {
        return 1;
    }

    return 0;
}