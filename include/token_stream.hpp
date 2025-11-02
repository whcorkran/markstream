#ifndef TOKEN_STRM_H
#define TOKEN_STRM_H

#include <vector>
#include <string>
#include <istream>

#include "../cmark-stream/src/cmark.h"

struct Token {
    std::string text;
    int32_t id;
};

class TokenStream {
    private:
        std::istream& in;
        std::vector<Token*> tokens;

    public:
        TokenStream(std::istream& input) : in(input) {}
 
        // flush
        bool has_next();
        std::string next();
        
        void reset();
        void clear();
        
        // accessors
        size_t position();
        size_t size();
        std::vector<std::string> data();
};

#endif // TOKEN_STRM_H
