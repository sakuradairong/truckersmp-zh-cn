// Operates only on the complete UI projection, before the native viewport slice.
#pragma once
#include <cstddef>
#include <cstring>

namespace playerfilterlogic {
enum class Mode : unsigned { All, Friends, Company, Staff };
inline constexpr std::size_t kRowSize=0x98;
inline bool ParseMode(const char* text,Mode& mode) {
    if(!text)return false;
    const char* names[]={"all","friends","company","staff"};
    for(unsigned i=0;i<4;++i)if(!std::strcmp(text,names[i])){mode=static_cast<Mode>(i);return true;}
    return false;
}
inline bool Matches(const unsigned char* row,Mode mode) {
    switch(mode) {
    case Mode::All:return true;
    case Mode::Friends:return row[0x1e]!=0;
    case Mode::Company:return row[0x1f]!=0;
    case Mode::Staff:return row[0x1c]!=0;
    default:return false;
    }
}
struct Counts {std::size_t candidates=0,matched=0;};
// The caller validates the range. Native strings must move and die through the
// native allocator; raw copying rows or replacing the source lists is forbidden.
template<class Move,class Erase>
Counts Compact(unsigned char* begin,std::size_t count,Mode mode,Move move,Erase erase) {
    Counts result{count,count};
    if(mode==Mode::All||!count)return result;
    std::size_t write=0;
    for(std::size_t read=0;read<count;++read)if(Matches(begin+read*kRowSize,mode)) {
        if(write!=read)move(begin+write*kRowSize,begin+read*kRowSize);
        ++write;
    }
    if(write!=count)erase(begin+write*kRowSize,begin+count*kRowSize);
    result.matched=write;
    return result;
}
}
