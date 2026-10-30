#include "DirectPlay.h"
#include <algorithm>

namespace dub {

DirectPlay::DirectPlay() { ready_.reserve (512); }

void DirectPlay::prepare (double sampleRate)
{
    window_ = std::max (1LL, (long long) (0.002 * sampleRate + 0.5));
    nHeld_ = 0;
    groupOpen_ = false;
    nGroup_ = 0;
    acidSounding_ = -1;
    pendingAcidOff_ = false;
    ready_.clear();
}

void DirectPlay::reset()
{
    nHeld_ = 0;
    groupOpen_ = false;
    nGroup_ = 0;
    ready_.clear();
    if (acidSounding_ >= 0) pendingAcidOff_ = true;
    acidSounding_ = -1;
}

void DirectPlay::addHeld (int note, int vel)
{
    for (int i = 0; i < nHeld_; ++i)
        if (held_[i].note == note)
        {
            for (int j = i; j < nHeld_ - 1; ++j) held_[j] = held_[j + 1];   // pressed again: it becomes the most recent
            --nHeld_;
            break;
        }
    if (nHeld_ == kMaxHeld)                                                   // more than 16 keys: forget the oldest
    {
        for (int j = 0; j < nHeld_ - 1; ++j) held_[j] = held_[j + 1];
        --nHeld_;
    }
    held_[nHeld_++] = { note, vel };
}

void DirectPlay::noteOn (long long at, int note, int velocity)
{
    note = std::min (127, std::max (0, note));
    const int vel = velocity >= 100 ? 127 : 90;
    if (acid_)
    {
        const bool legato = nHeld_ > 0;
        addHeld (note, vel);
        DirectCmd c;
        c.kind = legato ? DirectCmd::AcidSlide : DirectCmd::AcidNote;
        c.at = at; c.note = note; c.vel = vel;
        push (c);
        acidSounding_ = note;
        return;
    }
    if (groupOpen_ && at - groupStart_ >= window_) closeGroup (groupStart_ + window_);
    addHeld (note, vel);
    if (! groupOpen_) { groupOpen_ = true; groupStart_ = at; nGroup_ = 0; groupVel_ = 0; }
    bool known = false;
    for (int i = 0; i < nGroup_; ++i) known = known || group_[i] == note;
    if (! known && nGroup_ < kMaxHeld) group_[nGroup_++] = note;
    groupVel_ = std::max (groupVel_, vel);
}

void DirectPlay::noteOff (long long at, int note)
{
    int idx = -1;
    for (int i = 0; i < nHeld_; ++i) if (held_[i].note == note) idx = i;
    if (idx < 0) return;
    for (int j = idx; j < nHeld_ - 1; ++j) held_[j] = held_[j + 1];
    --nHeld_;
    if (acid_ && note == acidSounding_)
    {
        DirectCmd c;
        c.at = at;
        if (nHeld_ > 0)
        {
            c.kind = DirectCmd::AcidSlide; c.note = held_[nHeld_ - 1].note; c.vel = held_[nHeld_ - 1].vel;
            acidSounding_ = c.note;
        }
        else { c.kind = DirectCmd::AcidOff; acidSounding_ = -1; }
        push (c);
    }
}

void DirectPlay::allNotesOff (long long at)
{
    nHeld_ = 0;
    groupOpen_ = false;
    nGroup_ = 0;
    if (acidSounding_ >= 0)
    {
        DirectCmd c; c.kind = DirectCmd::AcidOff; c.at = at;
        push (c);
        acidSounding_ = -1;
    }
}

void DirectPlay::closeGroup (long long due)
{
    groupOpen_ = false;
    int all[2 * kMaxHeld];
    int n = 0;
    for (int i = 0; i < nGroup_; ++i) all[n++] = group_[i];
    const int groupCount = n;
    for (int i = 0; i < nHeld_; ++i) all[n++] = held_[i].note;

    // root = lowest of everything held or played in this chord
    int root = 127;
    for (int i = 0; i < n; ++i) root = std::min (root, all[i]);

    // notes of the stab: the chord's own notes (poly models), or everything held as well (STRINGS plays one chord at a time)
    const int count = strings_ ? n : groupCount;
    std::sort (all, all + (strings_ ? n : groupCount));
    int* end = std::unique (all, all + count);
    DirectCmd c;
    c.kind = DirectCmd::Chord;
    c.at = due;
    c.root = root;
    c.vel = groupVel_;
    int k = 0;
    for (int* p = all; p != end && k < kMaxChordNotes; ++p) c.notes[(size_t) k++] = *p;
    c.count = k;
    nGroup_ = 0;
    if (k > 0) push (c);
}

void DirectPlay::flush (long long blockStart, long long blockEnd, std::vector<DirectCmd>& out)
{
    if (pendingAcidOff_)
    {
        DirectCmd c; c.kind = DirectCmd::AcidOff; c.at = blockStart;
        push (c);
        pendingAcidOff_ = false;
    }
    if (groupOpen_ && groupStart_ + window_ < blockEnd) closeGroup (std::max (blockStart, groupStart_ + window_));
    if (ready_.empty()) return;
    std::stable_sort (ready_.begin(), ready_.end(), [] (const DirectCmd& a, const DirectCmd& b) { return a.at < b.at; });
    for (auto& c : ready_)
    {
        if (c.at < blockStart) c.at = blockStart;
        if (c.at >= blockEnd) c.at = blockEnd - 1;
        out.push_back (c);
    }
    ready_.clear();
}

} // namespace dub
