#pragma once
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtGlobal>

// The controller launches detached PipeWire graphs and has to be able to stop every one of
// them. Remembering a single pid per kind is not enough: when a graph is replaced while its
// predecessor is still exiting, the older pid is overwritten and that process becomes
// unaddressable forever, so it keeps holding the stable linux_mixer.* node names and its
// ports stay linked. The ledger here names every graph this state root launched, and only a
// process whose own command line points back at this state root is ever considered, so a
// stale or hand-edited entry can never make us signal somebody else's pid.
namespace owned_processes {

// True only if `pid` is running and was launched by this state root for this kind
// ("engine", "matrix", "aux" or "monitor"). Any other kind never matches.
bool matches(const QString& runtime,const QString& kind,qint64 pid);

// Recorded pids for `kind` that are still ours and still running, oldest first.
QList<qint64> live(const QString& runtime,const QString& kind);

// Newest running pid for `kind`, or 0. Callers treat a non-zero value as "our graph exists".
qint64 newest(const QString& runtime,const QString& kind);

// Append a pid we just launched. Entries that no longer match are dropped here, which is
// what keeps the file from accumulating recycled pids.
void record(const QString& runtime,const QString& kind,qint64 pid);

// Adopt graphs that carry this state root's own conf path but are not recorded, and return
// how many were added. A pid that was lost to an overwrite or a failed write is otherwise
// unreachable forever: it would keep holding resources while no stop() could ever name it.
// Adoption is deliberately not used to decide whether a graph may launch, and it can only
// ever find processes this state root's own configuration started.
int adopt(const QString& runtime,const QString& kind);

// Terminate, then force-terminate, everything recorded for `kind`. Leaves anything that
// survived in the ledger so a later attempt still knows about it. True when nothing is left.
bool stopAll(const QString& runtime,const QString& kind,int termMs=1500,int killMs=500);

}
