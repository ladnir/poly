#include <memory>
#include <typeinfo>
#include <typeindex>
#include <type_traits>
#include <utility>

namespace poly
{

    template<typename T>
    struct storage_concept
    {
        storage_concept() = default;
        virtual ~storage_concept() = default;

        // Type eased move capable of moving 'this' to local or remote storage.
        // @ storage - The new storage location of this object of it fits.
        // @ storageSize - The size of the new storage.
        virtual void move_to(void* storage, size_t  /*storageSize*/) = 0;

        // Getters. Default to empty. 
        virtual const T* get() const = 0;
        virtual T* get() = 0;

        // Utility for showing if item is small buffer optimized.
        virtual bool is_local() const = 0;
    };

    template<typename T>
    struct empty_storage : public storage_concept<T>
    {
        void move_to(void* storage, size_t storageSize) override { new (storage) empty_storage(); }
        const T* get() const override { return nullptr; }
        T* get() override { return nullptr; }
        bool is_local() const override { return true; }
    };

    // The wrapper for to mannage inline objects.
    template<typename U, typename T>
    struct inline_storage : storage_concept<T>
    {
        template<typename... Args>
        explicit inline_storage(Args&&... args)
            :_u(std::forward<Args>(args)...) {}

        void move_to(void* storage, size_t storageSize) override;
        const T* get() const override { return reinterpret_cast<const T*>(&_u); }
        T* get() override { return reinterpret_cast<T*>(&_u); }
        bool is_local() const override { return true; }
        U _u;
    };

    // The wrapper for to mannage heap objects.
    template<typename U, typename T, bool can_be_inlined = true>
    struct heap_storage : storage_concept<T>
    {
        explicit heap_storage(U* u) : _u(u) {}

        void move_to(void* storage, size_t storageSize) override;
        const T* get() const override { return reinterpret_cast<const T*>(_u.get()); }
        T* get() override { return reinterpret_cast<T*>(_u.get()); }
        bool is_local() const override { return false; }
        std::unique_ptr<U> _u;
    };

    // Implementatin for moving a inline object to a new storage location.
    template<typename U, typename T>
    void inline_storage<U, T>::move_to(void* storage, size_t storageSize)
    {
        if (storageSize <= sizeof(inline_storage<U, T>)) {
            new (storage) inline_storage<U, T>(std::move(_u));
        }
        else {
            new (storage) heap_storage<U, T>(new U(std::move(_u)));
        }
    }

    // Implementatin for moving a heap object to a new storage location.
    template<typename U, typename T, bool can_be_inlined>
    void heap_storage<U, T, can_be_inlined>::move_to(void * storage, size_t storageSize)
    {
        if (storageSize >= sizeof(inline_storage<U, T>) &&
            _u != nullptr &&
            can_be_inlined)
        {
            new (storage) inline_storage<U, T>(std::move(*_u));
            _u.reset();
        }
        else {
            new (storage) heap_storage<U, T, can_be_inlined>(_u.release());
        }
    }

    
    template<typename U, size_t storage_size>
    constexpr bool is_small = (sizeof(U) <= storage_size);

    template<typename U, typename T>
    constexpr bool is_acceptable = std::is_convertible<U*, T*>::value;

    // Small buffer optimized unique pointer.
    template<typename T, size_t storage_size = 120 /* makes the whole thing 128 bytes */>
    class unique_ptr
    {
        static_assert(std::is_pointer<T>::value == false, "must not be a pointer");
        using this_type = unique_ptr<T, storage_size>;
    public:

        using posize_ter = T * ;
        using element_type = T;
        // Large enough for a vtable pointer and at least a pointer to the heap.
        using storage_type = typename std::aligned_storage<
            sizeof(void*) + (sizeof(void*)> storage_size ? sizeof(void*) : storage_size)
        >::type;

        // constructors
        unique_ptr() {
            new (&_storage) empty_storage<T>();
        }

        unique_ptr(const unique_ptr<T>&) = delete;

        // Generalized move function for arbitrary local storage size.
        template<typename U, size_t other_size,
            typename Enabled =
            typename std::enable_if<is_acceptable<U, T>>::type
        >
            explicit unique_ptr(unique_ptr<U, other_size>&& m) {
            m.get_base().move_to(&_storage, sizeof(storage_type));
        }

        ~unique_ptr() {
            destruct();
        }

        // Move operator= from another unique_ptr. Moves U to local storage
        // if U will fit, otherwise places U on the heap (if its not there already).
        template<typename U, size_t other_size>
        typename std::enable_if<is_acceptable<U, T>,
            this_type&>::type
            operator=(unique_ptr<U, other_size>&& m)
        {
            destruct();
            m.get_base().move_to(&_storage, sizeof(storage_type));
            return *this;
        }

        // Move operator= from a U which is derived from T. Moves U to local storage.
        template<typename U>
        typename std::enable_if<is_acceptable<U, T>,                    // U* can cast as T* and should be inlined.
            this_type&>::type // return *this
            operator=(U&& u)
        {
            destruct();
            if constexpr(is_small<U, storage_size>)
                new (&_storage) inline_storage<U, T>(std::forward<U>(u));
            else
                new (&_storage) heap_storage<U, T>(new U(std::forward<U>(u)));

            return *this;
        }

        template<typename U>
        typename std::enable_if<is_acceptable<U, T>>::type                                // return void
            reset(U* u)
        {
            destruct();

            if constexpr(is_small<U, storage_size>)
            {
                // Check if u is actaully of type U before placing it in local
                // storage. This check is required to prevent slicing. If false,
                // make sure its never inlined.
                if (typeid(*u).hash_code() == typeid(U).hash_code())
                    new (&_storage) inline_storage<U, T>(std::move(*u));
                else
                    new (&_storage) heap_storage<U, T, false>(u);
            }
            else
            {
                new (&_storage) heap_storage<U, T>(u);
            }
        }


        // Inplace constructors. (No if constexper due to visual studio bug when composed with std::forward)
        template<typename U, typename... Args >
        typename std::enable_if<
            is_acceptable<U, T> &&
            is_small<U, storage_size> &&
            std::is_constructible<U, Args...>::value            // U is constructable from Args...
        >::type                                                 // return void
            emplace(Args&&... args)
        {
            destruct();
            new (&_storage) inline_storage<U, T>(std::forward<Args>(args)...);
        }

        template<typename U, typename... Args >
        typename std::enable_if<
            is_acceptable<U, T> &&
            is_small<U, storage_size> == false &&
            std::is_constructible<U, Args...>::value            // U is constructable from Args...
        >::type                                                 // return void
            emplace(Args&&... args)
        {
            destruct();
            new (&_storage) heap_storage<U, T>(new U(std::forward<Args>(args)...));
        }

        void reset()
        {
            destruct();
            new (&_storage) empty_storage<T>();
        }

        T* operator->() { return get(); }
        const T* operator->() const { return get(); }

        T* get() { return get_base().get(); }
        const T* get() const { return get_base().get(); }

        explicit operator bool() const noexcept
        {
            return get() != nullptr;
        }

        bool is_local() const { return get_base().is_local(); }

    private:

        void destruct()
        {
            // manually call the virtual destructor.
            get_base().~storage_concept();
        }

        storage_type _storage;

        storage_concept<T>& get_base() { return *(storage_concept<T>*)&_storage; }
        const storage_concept<T>& get_base() const { return *(storage_concept<T>*)&_storage; }


        template <typename, size_t>
        friend class unique_ptr;
    };


    template<typename T, typename U, typename... Args>
    typename  std::enable_if<
        std::is_constructible<U, Args...>::value &&
        (std::is_base_of<T, U>::value ||
            std::is_same<T, U>::value)
        , unique_ptr<T>>::type
        make_unique_ptr(Args&&... args)
    {
        unique_ptr<T> t;
        t.template emplace<U>(std::forward<Args>(args)...);
        return std::move(t);
    }

} // namespace poly


