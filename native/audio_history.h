#pragma once
#include <QtCore/QByteArray>
#include <algorithm>
#include <cstring>

// Fixed-size pre-record buffer. Appending never shifts older audio.
class AudioHistory {
    QByteArray storage;
    qsizetype next=0,used=0;
public:
    void clear(){storage.clear();next=used=0;}
    void setCapacity(qsizetype bytes){
        bytes=std::max<qsizetype>(0,bytes);
        if(storage.size()==bytes)return;
        storage.resize(bytes);next=used=0;
    }
    void append(const QByteArray& audio){
        const auto capacity=storage.size();if(!capacity||audio.isEmpty())return;
        const auto count=std::min(capacity,audio.size());
        const char* source=audio.constData()+audio.size()-count;
        const auto first=std::min(count,capacity-next);
        std::memcpy(storage.data()+next,source,first);
        if(count>first)std::memcpy(storage.data(),source+first,count-first);
        next=(next+count)%capacity;used=std::min(capacity,used+count);
    }
    QByteArray tail(qsizetype wanted)const{
        const auto count=std::clamp<qsizetype>(wanted,0,used);
        if(!count)return {};
        const auto capacity=storage.size(),start=(next+capacity-count)%capacity;
        const auto first=std::min(count,capacity-start);
        QByteArray result(count,Qt::Uninitialized);
        std::memcpy(result.data(),storage.constData()+start,first);
        if(count>first)std::memcpy(result.data()+first,storage.constData(),count-first);
        return result;
    }
};
