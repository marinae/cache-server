#ifndef __PARSER_H__
#define __PARSER_H__

#include <string>
#include <vector>

//+----------------------------------------------------------------------------+
//| Parser class                                                               |
//+----------------------------------------------------------------------------+

class CParser {
    static std::string cutSpaces(std::string line);

public:
    static int parseLine(std::string line, std::string *key,
                         std::string *value, int *ttl);
};

#endif /* __PARSER_H__ */