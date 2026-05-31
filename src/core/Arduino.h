#pragma once

// Programa clave para el desarrollo de tests unitarios. 
// Si se compila con NATIVE_TEST definido, se incluyen stubs de String y Serial para permitir la ejecución de tests en un entorno nativo (PC).
// Si no se define NATIVE_TEST, se incluye el Arduino.h real para compilar en el entorno de Arduino.
#ifdef NATIVE_TEST

#include <stdint.h>
#include <string>
#include <cctype>
#include <cstring>

//Stub de String
class String
{
public:
    std::string _str;

    String() {}
    String(const char *s) : _str(s ? s : "") {}
    String(const std::string &s) : _str(s) {}
    String(int v) : _str(std::to_string(v)) {}
    String(unsigned int v) : _str(std::to_string(v)) {}
    String(long v) : _str(std::to_string(v)) {}
    String(unsigned long v) : _str(std::to_string(v)) {}

    const char *c_str() const { return _str.c_str(); }
    int length() const { return _str.length(); }

    String operator+(const String &rhs) const { return String(_str + rhs._str); }
    String operator+(const char *rhs) const { return String(_str + rhs); }
    String &operator+=(const String &rhs)
    {
        _str += rhs._str;
        return *this;
    }
    String &operator+=(const char *rhs)
    {
        _str += rhs;
        return *this;
    }
    String &operator+=(char c)
    {
        _str += c;
        return *this;
    }
    bool operator==(const String &rhs) const { return _str == rhs._str; }
    bool operator==(const char *rhs) const { return _str == rhs; }
    bool operator<(const String &rhs) const { return _str < rhs._str; }

    //Substring
    String substring(int from, int to) const
    {
        if (from < 0 || to < from || from >= (int)_str.size())
            return String("");

        if (to > (int)_str.size())
            to = _str.size();

        return String(_str.substr(from, to - from));
    }

    int indexOf(char c, int from = 0) const
    {
        size_t pos = _str.find(c, from);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    int indexOf(const String &s, int from = 0) const
    {
        size_t pos = _str.find(s._str, from);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    bool startsWith(const char *s, int from = 0) const { return _str.compare(from, strlen(s), s) == 0; }
    void reserve(int size) { _str.reserve(size); }

    char operator[](int index) const { return _str[index]; }
};

inline String operator+(const char *lhs, const String &rhs) { return String(lhs) + rhs; }

//Stub de Serial
struct SerialMock
{
    void print(const char *) {}
    void print(int) {}
    void print(const String &) {}
    void println(const char *) {}
    void println(int) {}
    void println(const String &) {}
    void printf(const char *, ...) {}
};

extern SerialMock Serial;

void delay(unsigned long ms);
inline bool isDigit(char c) { return std::isdigit(c); }

//Constantes de configuración de pines y estados
#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif
#ifndef INPUT
#define INPUT 0
#endif
#ifndef OUTPUT
#define OUTPUT 1
#endif

#else

#include <Arduino.h>

#endif