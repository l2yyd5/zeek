// See the file "COPYING" in the main distribution directory for copyright.

#include "zeek/probabilistic/Topk.h"

#include <broker/error.hh>

#include "zeek/CompHash.h"
#include "zeek/Dict.h"
#include "zeek/Reporter.h"
#include "zeek/broker/Data.h"

namespace zeek::probabilistic::detail {

static void topk_element_hash_delete_func(void* val) {
    Element* e = (Element*)val;
    delete e;
}

void TopkVal::Typify(TypePtr t) {
    assert(! hash && ! type);
    type = std::move(t);
    auto tl = make_intrusive<TypeList>(type);
    tl->Append(type);
    hash = new zeek::detail::CompositeHash(std::move(tl));
}

zeek::detail::HashKey* TopkVal::GetHash(Val* v) const {
    auto key = hash->MakeHashKey(*v, true);
    assert(key);
    return key.release();
}

TopkVal::TopkVal(uint64_t arg_size) : OpaqueVal(topk_type) {
    elementDict = new PDict<Element>;
    elementDict->SetDeleteFunc(topk_element_hash_delete_func);
    size = arg_size;
    numElements = 0;
    pruned = false;
    hash = nullptr;
}

TopkVal::TopkVal() : OpaqueVal(topk_type) {
    elementDict = new PDict<Element>;
    elementDict->SetDeleteFunc(topk_element_hash_delete_func);
    size = 0;
    numElements = 0;
    hash = nullptr;
}

TopkVal::~TopkVal() {
    elementDict->Clear();
    delete elementDict;

    // now all elements are already gone - delete the buckets
    std::list<Bucket*>::iterator bi = buckets.begin();
    while ( bi != buckets.end() ) {
        delete *bi;
        bi++;
    }

    delete hash;
}

void TopkVal::Merge(const TopkVal* value, bool doPrune) {
    if ( ! value->type ) {
        // Merge-from is empty. Nothing to do.
        assert(value->numElements == 0);
        return;
    }

    if ( type == nullptr ) {
        assert(numElements == 0);
        Typify(value->type);
    }

    else {
        if ( ! same_type(type, value->type) ) {
            reporter->Error("Cannot merge top-k elements of differing types.");
            return;
        }
    }

    std::list<Bucket*>::const_iterator it = value->buckets.begin();
    while ( it != value->buckets.end() ) {
        Bucket* b = *it;
        uint64_t currcount = b->count;
        std::list<Element*>::const_iterator eit = b->elements.begin();

        while ( eit != b->elements.end() ) {
            Element* e = *eit;
            // lookup if we already know this one...
            zeek::detail::HashKey* key = GetHash(e->value);
            Element* olde = (Element*)elementDict->Lookup(key);

            if ( olde == nullptr ) {
                olde = new Element();
                olde->epsilon = 0;
                olde->value = e->value;
                // insert at bucket position 0
                if ( buckets.size() > 0 ) {
                    assert(buckets.front()->count > 0);
                }

                Bucket* newbucket = new Bucket();
                newbucket->count = 0;
                newbucket->bucketPos = buckets.insert(buckets.begin(), newbucket);

                olde->parent = newbucket;
                newbucket->elements.insert(newbucket->elements.end(), olde);

                elementDict->Insert(key, olde);
                numElements++;
            }

            // now that we are sure that the old element is present - increment epsilon
            olde->epsilon += e->epsilon;

            // and increment position...
            IncrementCounter(olde, currcount);
            delete key;

            eit++;
        }

        it++;
    }

    // now we have added everything. And our top-k table could be too big.
    // prune everything...

    assert(size > 0);

    if ( ! doPrune )
        return;

    while ( numElements > size ) {
        pruned = true;
        assert(buckets.size() > 0);
        Bucket* b = buckets.front();
        assert(b->elements.size() > 0);

        Element* e = b->elements.front();
        zeek::detail::HashKey* key = GetHash(e->value);
        elementDict->RemoveEntry(key);
        delete key;
        delete e;

        b->elements.pop_front();

        if ( b->elements.size() == 0 ) {
            delete b;
            buckets.pop_front();
        }

        numElements--;
    }
}

ValPtr TopkVal::DoClone(CloneState* state) {
    auto clone = make_intrusive<TopkVal>(size);
    clone->Merge(this);
    return state->NewClone(this, std::move(clone));
}

VectorValPtr TopkVal::GetTopK(int k) const // returns vector
{
    if ( numElements == 0 ) {
        reporter->Error("Cannot return topk of empty");
        return nullptr;
    }

    auto v = make_intrusive<VectorType>(type);
    auto t = make_intrusive<VectorVal>(std::move(v));

    // this does no estimation if the results is correct!
    // in any case - just to make this future-proof (and I am lazy) - this can return more than k.

    int read = 0;
    std::list<Bucket*>::const_iterator it = buckets.end();
    it--;
    while ( read < k ) {
        // printf("Bucket %llu\n", (*it)->count);
        std::list<Element*>::iterator eit = (*it)->elements.begin();
        while ( eit != (*it)->elements.end() ) {
            // printf("Size: %ld\n", (*it)->elements.size());
            t->Assign(read, (*eit)->value);
            read++;
            eit++;
        }

        if ( it == buckets.begin() )
            break;

        it--;
    }

    return t;
}

uint64_t TopkVal::GetCount(Val* value) const {
    zeek::detail::HashKey* key = GetHash(value);
    Element* e = (Element*)elementDict->Lookup(key);
    delete key;

    if ( e == nullptr ) {
        reporter->Error("GetCount for element that is not in top-k");
        return 0;
    }

    return e->parent->count;
}

uint64_t TopkVal::GetEpsilon(Val* value) const {
    zeek::detail::HashKey* key = GetHash(value);
    Element* e = (Element*)elementDict->Lookup(key);
    delete key;

    if ( e == nullptr ) {
        reporter->Error("GetEpsilon for element that is not in top-k");
        return 0;
    }

    return e->epsilon;
}

uint64_t TopkVal::GetSum() const {
    uint64_t sum = 0;

    std::list<Bucket*>::const_iterator it = buckets.begin();
    while ( it != buckets.end() ) {
        sum += (*it)->elements.size() * (*it)->count;

        it++;
    }

    if ( pruned )
        reporter->Warning(
            "TopkVal::GetSum() was used on a pruned data structure. Result values do "
            "not represent total element count");

    return sum;
}

void TopkVal::Encountered(ValPtr encountered) {
    // ok, let's see if we already know this one.

    if ( numElements == 0 )
        Typify(encountered->GetType());
    else if ( ! same_type(type, encountered->GetType()) ) {
        reporter->Error("Trying to add element to topk with differing type from other elements");
        return;
    }

    // Step 1 - get the hash.
    zeek::detail::HashKey* key = GetHash(encountered);
    Element* e = (Element*)elementDict->Lookup(key);

    if ( e == nullptr ) {
        e = new Element();
        e->epsilon = 0;
        e->value = std::move(encountered);

        // well, we do not know this one yet...
        if ( numElements < size ) {
            // brilliant. just add it at position 1
            if ( buckets.size() == 0 || (*buckets.begin())->count > 1 ) {
                Bucket* b = new Bucket();
                b->count = 1;
                std::list<Bucket*>::iterator pos = buckets.insert(buckets.begin(), b);
                b->bucketPos = pos;
                b->elements.insert(b->elements.end(), e);
                e->parent = b;
            }
            else {
                Bucket* b = *buckets.begin();
                assert(b->count == 1);
                b->elements.insert(b->elements.end(), e);
                e->parent = b;
            }

            elementDict->Insert(key, e);
            numElements++;
            delete key;

            return; // done. it is at pos 1.
        }

        else {
            // replace element with min-value
            Bucket* b = *buckets.begin(); // bucket with smallest elements

            // evict oldest element with least hits.
            assert(b->elements.size() > 0);
            zeek::detail::HashKey* deleteKey = GetHash((*(b->elements.begin()))->value);
            b->elements.erase(b->elements.begin());
            Element* deleteElement = (Element*)elementDict->RemoveEntry(deleteKey);
            assert(deleteElement); // there has to have been a minimal element...
            delete deleteElement;
            delete deleteKey;

            // and add the new one to the end
            e->epsilon = b->count;
            b->elements.insert(b->elements.end(), e);
            elementDict->Insert(key, e);
            e->parent = b;

            // fallthrough, increment operation has to run!
        }
    }

    // ok, we now have an element in e
    delete key;
    IncrementCounter(e); // well, this certainly was anticlimactic.
}

// increment by count
void TopkVal::IncrementCounter(Element* e, unsigned int count) {
    Bucket* currBucket = e->parent;
    uint64_t currcount = currBucket->count;

    // well, let's test if there is a bucket for currcount++
    std::list<Bucket*>::iterator bucketIter = currBucket->bucketPos;

    Bucket* nextBucket = nullptr;

    bucketIter++;

    while ( bucketIter != buckets.end() && (*bucketIter)->count < currcount + count )
        bucketIter++;

    if ( bucketIter != buckets.end() && (*bucketIter)->count == currcount + count )
        nextBucket = *bucketIter;

    if ( nextBucket == nullptr ) {
        // the bucket for the value that we want does not exist.
        // create it...

        Bucket* b = new Bucket();
        b->count = currcount + count;

        std::list<Bucket*>::iterator nextBucketPos = buckets.insert(bucketIter, b);
        b->bucketPos = nextBucketPos; // and give it the iterator we know now.

        nextBucket = b;
    }

    // ok, now we have the new bucket in nextBucket. Shift the element over...
    currBucket->elements.remove(e);
    nextBucket->elements.insert(nextBucket->elements.end(), e);

    e->parent = nextBucket;

    // if currBucket is empty, we have to delete it now
    if ( currBucket->elements.size() == 0 ) {
        buckets.remove(currBucket);
        delete currBucket;
        currBucket = nullptr;
    }
}

IMPLEMENT_OPAQUE_VALUE(TopkVal)

std::optional<BrokerData> TopkVal::DoSerialize() const {
    BrokerListBuilder builder;
    builder.Reserve(8);

    builder.Add(size);
    builder.Add(numElements);
    builder.Add(pruned);

    if ( type ) {
        auto t = SerializeType(type);
        if ( ! t )
            return std::nullopt;

        builder.Add(std::move(*t));
    }
    else
        builder.AddNil();

    uint64_t i = 0;
    for ( const auto* b : buckets ) {
        builder.AddCount(b->elements.size());
        builder.AddCount(b->count);

        for ( const auto* element : b->elements ) {
            builder.AddCount(element->epsilon);
            BrokerData val;
            if ( ! val.Convert(element->value) )
                return std::nullopt;

            builder.Add(std::move(val));

            i++;
        }
    }

    assert(i == numElements);
    return std::move(builder).Build();
}

bool TopkVal::DoUnserialize(BrokerDataView data) {
    if ( ! data.IsList() )
        return false;

    auto v = data.ToList();
    if ( v.Size() < 4 || ! v[0].IsCount() || ! v[1].IsCount() || ! v[2].IsBool() )
        return false;

    size = v[0].ToCount();
    numElements = v[1].ToCount();
    pruned = v[2].ToBool();

    if ( ! v[3].IsNil() ) {
        auto t = UnserializeType(v[3]);

        if ( ! t )
            return false;

        Typify(t);
    }

    bool ok = true;
    auto index = size_t{4}; // Index into v.
    auto atEnd = [&v, &index] { return index >= v.Size(); };
    // Returns the element  at the given index in v, if that element is a count.
    // If so, ok becomes true, and the index gets incremented.
    // If not, ok becomes false, and the index remains unchanged.
    auto nextCount = [&v, &ok, &index]() -> uint64_t {
        if ( index >= v.Size() || ! v[index].IsCount() ) {
            ok = false;
            return 0;
        }
        auto res = v[index].ToCount();
        ++index;
        return res;
    };

    uint64_t i = 0;
    while ( i < numElements ) {
        auto elements_count = nextCount();
        if ( ! ok )
            return false;

        auto count = nextCount();
        if ( ! ok )
            return false;

        auto* b = new Bucket();
        b->count = count;
        b->bucketPos = buckets.insert(buckets.end(), b);

        for ( uint64_t j = 0; j < elements_count; j++ ) {
            auto epsilon = nextCount();
            if ( ! ok )
                return false;

            if ( atEnd() )
                return false;

            auto val = v[index++].ToVal(type.get());

            if ( ! val )
                return false;

            Element* e = new Element();
            e->epsilon = epsilon;
            e->value = std::move(val);
            e->parent = b;

            b->elements.insert(b->elements.end(), e);

            zeek::detail::HashKey* key = GetHash(e->value);
            assert(elementDict->Lookup(key) == nullptr);

            elementDict->Insert(key, e);
            delete key;
            ++i;
        }
    }

    if ( ! atEnd() )
        return false;

    return true;
}

} // namespace zeek::probabilistic::detail
