#include "audio_history.h"
#include <cassert>

int main(){
    AudioHistory history;
    QByteArray expected;
    // Different chunk sizes exercise partial wraps and chunks larger than
    // the buffer, as happens when the GUI catches up after a stall.
    for(int capacity:{0,1,7,64,4096}){
        history.clear();history.setCapacity(capacity);expected.clear();
        for(int step=0;step<200;++step){
            QByteArray chunk((step*137)%8192,Qt::Uninitialized);
            for(qsizetype i=0;i<chunk.size();++i)chunk[i]=char(step+i);
            expected+=chunk;
            if(expected.size()>capacity)expected.remove(0,expected.size()-capacity);
            history.append(chunk);
            for(int wanted:{0,1,5,10000})assert(history.tail(wanted)==expected.right(std::min<qsizetype>(wanted,expected.size())));
        }
    }
    history.setCapacity(12);assert(history.tail(100).isEmpty());
    history.append("abcdefghijklmnop");assert(history.tail(100)=="efghijklmnop");
    history.clear();assert(history.tail(100).isEmpty());
}
