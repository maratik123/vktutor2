#ifndef CONSTLATIN1STRING_H
#define CONSTLATIN1STRING_H

#include <QLatin1String>

struct ConstLatin1String : QLatin1String
{
    explicit constexpr ConstLatin1String(const char* const s) :
        QLatin1String{s, static_cast<int>(std::char_traits<char>::length(s))} {}
};

#endif // CONSTLATIN1STRING_H
