#pragma once
#include <string>
#include <cctype>

struct MathEngine {
    std::string expr;
    size_t pos = 0;

    double parseExpression() {
        double x = parseTerm();
        for (;;) {
            if (eat('+')) x += parseTerm();
            else if (eat('-')) x -= parseTerm();
            else return x;
        }
    }

    double parseTerm() {
        double x = parseFactor();
        for (;;) {
            if (eat('*')) x *= parseFactor();
            else if (eat('/')) {
                double d = parseFactor();
                x = (d != 0) ? x / d : 0;
            } else return x;
        }
    }

    double parseFactor() {
        if (eat('(')) { double x = parseExpression(); eat(')'); return x; }
        size_t start = pos;
        if ((expr[pos] >= '0' && expr[pos] <= '9') || expr[pos] == '.' || expr[pos] == '-') {
            if (expr[pos] == '-') pos++;
            while (pos < expr.size() && ((expr[pos] >= '0' && expr[pos] <= '9') || expr[pos] == '.')) pos++;
            return std::stod(expr.substr(start, pos - start));
        }
        return 0;
    }

    bool eat(char c) {
        while (pos < expr.size() && std::isspace(expr[pos])) pos++;
        if (pos < expr.size() && expr[pos] == c) { pos++; return true; }
        return false;
    }
};