#include <memory>
#include <typeinfo>
#include <typeindex>
#include <type_traits>
#include <utility>

namespace poly_v2
{



    template<typename T>
    struct concept {
        void(*_dtor)(void*) noexcept;
        void(*_move)(const concept<T>*&, void*, void*, size_t) noexcept;
        T*(*_get)(void*) noexcept;
        T*(*_release)(void*) noexcept;
        bool(*_is_inlined)() noexcept;
    };

    template<typename T>
    struct empty_model {
        static void _dtor(void* self) noexcept {}
        static void _move(const concept<T>*& c, void*, void*, size_t) noexcept { c = &empty_model::vtable; }
        static T* _get(void* self) noexcept { return nullptr; }
        static T* _release(void* self) noexcept { return nullptr; }
        static bool _is_inlined() noexcept { return true; }
        static constexpr concept<T> vtable{ _dtor, _move, _get,_release, _is_inlined };
    };

    template<typename U, typename T> struct inline_model;
    template<typename U, typename T> struct ptr_model;

    template<typename U, typename T>
    struct inline_model {

        template<typename... Args>
        inline_model(const concept<T>*& c, Args&&... args)
            : _u(std::forward<Args>(args)...)
        {
            c = &vtable;
        }

        static void _dtor(void* self) noexcept {
            static_cast<inline_model*>(self)->~inline_model();
        }

        static void _move(const concept<T>*& c, void* self, void* dest, size_t dest_size) noexcept {
            if (sizeof(inline_model) <= dest_size)
                new (dest) inline_model(c, std::move(static_cast<inline_model*>(self)->_u));
            else
                new (dest) ptr_model<U, T>(c, new U(std::move(static_cast<inline_model*>(self)->_u)));
        }

        static T* _get(void* self) noexcept {
            return &static_cast<inline_model*>(self)->_u;
        }

        static T* _release(void* self) noexcept {
            return new U(std::move(static_cast<inline_model*>(self)->_u));
        }

        static bool _is_inlined() noexcept { return true; }

        static constexpr concept<T> vtable{ _dtor, _move, _get,_release, _is_inlined };

        U _u;
    };

    template<typename U, typename T>
    struct ptr_model {

        ptr_model(const concept<T>*& c, U* u) : _u(u) {
            c = &vtable;
        }

        template<typename... Args>
        ptr_model(const concept<T>*& c, Args&&... args)
            : _u(new U(std::forward<Args>(args)...))
        {
            c = &vtable;
        }

        static void _dtor(void* self) noexcept {
            static_cast<ptr_model*>(self)->~ptr_model();
        }

        static void _move(const concept<T>*& c, void* _self, void* dest, size_t dest_size) noexcept {
            auto self = static_cast<ptr_model*>(_self);

            if (sizeof(inline_model<U, T>) <= dest_size)
                new (dest) inline_model<U, T>(c, std::move(*self->_u.get()));
            else
                new (dest) ptr_model(c, self->_u.release());
        }

        static T* _get(void* self) noexcept {
            return static_cast<ptr_model*>(self)->_u.get();
        }

        static T* _release(void* self) noexcept {
            return static_cast<ptr_model*>(self)->_u.release();
        }

        static bool _is_inlined() noexcept { return true; }

        static constexpr concept<T> vtable{ _dtor, _move, _get,_release, _is_inlined };

        std::unique_ptr<U> _u;
    };

    template<typename U>
    struct in_place
    {
        using value_type = U;
        in_place() = default;
    };

    template<typename U, typename T, size_t storage_size>
    constexpr bool is_small = (sizeof(inline_model<std::decay_t<U>, T>) <= storage_size);

    template<typename U, typename T>
    constexpr bool is_acceptable = std::is_convertible_v<U*, T*>;

    // Small buffer optimized unique pointer.
    template<typename T, size_t storage_size = 128>
    class unique_ptr
    {
        using this_type = unique_ptr<T, storage_size>;
        using storage_type = typename std::aligned_storage_t<storage_size>;

        const concept<T>* _concept = &empty_model<T>::vtable;
        storage_type _model;

    public:

        unique_ptr() = default;


        template<typename U, size_t other_size,
            typename Enabled = std::enable_if_t<is_acceptable<U, T>>>
            unique_ptr(unique_ptr<U, other_size>&& p) noexcept
        {
            p._concept->_move(_concept, &p._model, &_model, sizeof(storage_type));
        }

        template<typename U, typename Enabled = std::enable_if_t<is_acceptable<U, T>>>
        unique_ptr(U* u) { reset(u); }

        template<typename U, typename Enabled = std::enable_if_t<is_acceptable<U, T>>>
        unique_ptr(U* u, bool no_inline) { reset(u, no_inline); }

        ~unique_ptr() { reset(); }

        template<typename U, size_t other_size>
        typename std::enable_if_t<is_acceptable<U, T>,
            this_type&>
            operator=(unique_ptr<U, other_size>&& p)
        {
            reset();
            p._concept->_move(_concept, &p._model, &_model, sizeof(storage_type));
            return *this;
        }

        template<typename U>
        typename std::enable_if_t<is_acceptable<U, T>,
            this_type&>
            operator=(U&& u)
        {
            reset();
            if constexpr (is_small<U, T, storage_size>)
                new (&_model) inline_model<std::decay_t<U>, T>(_concept, std::forward<U>(u));
            else
                new (&_model) ptr_model<std::decay_t<U>, T>(_concept, new U(std::forward<U>(u)));

            return *this;
        }

        // Inplace constructors. (No if constexper due to visual studio bug when composed with std::forward)
        template<typename U, typename... Args >
        typename std::enable_if_t<
            is_acceptable<U, T> &&
            is_small<U, T, storage_size> &&
            std::is_constructible_v<std::decay_t<U>, Args...>>
            emplace(Args&&... args)
        {
            reset();
            new (&_model) inline_model<std::decay_t<U>, T>(_concept, std::forward<Args>(args)...);
        }

        template<typename U, typename... Args >
        typename std::enable_if_t<
            is_acceptable<U, T> &&
            is_small<U, T, storage_size> == false &&
            std::is_constructible_v<std::decay_t<U>, Args...>>
            emplace(Args&&... args)
        {
            reset();
            new (&_model) ptr_model<std::decay_t<U>, T>(_concept, std::forward<Args>(args)...);
        }


        template<typename U>
        typename std::enable_if_t<is_acceptable<U, T>>
            reset(U*u)
        {
            reset();
            if constexpr (is_small<U, T, storage_size>)
                new (&_model) inline_model<std::decay_t<U>, T>(_concept, std::move(*u));
            else
                new (&_model) ptr_model<std::decay_t<U>, T>(_concept, u);
        }



        template<typename U>
        typename std::enable_if_t<is_acceptable<U, T>>
            reset(U*u, bool no_inline)
        {
            if (no_inline)
            {
                reset();
                new (&_model) ptr_model<std::decay_t<U>, T, false>(_concept, u);
            }
            else
                reset(u);
        }

        void reset()
        {
            _concept->_dtor(&_model);
            _concept = &empty_model<T>::vtable;
        }


        template<typename U = T>
        typename std::enable_if_t<is_acceptable<U,T>, U*>
            release()
        {
            auto ptr = dynamic_cast<U*>(get());
            if (ptr)
                return static_cast<U*>(_concept->_release(&_model));
            
            return nullptr;
        }


        T* operator->() noexcept { return get(); }
        const T* operator->() const noexcept { return get(); }

        T* get() noexcept { return _concept->_get(&_model); }
        const T* get() const noexcept { return _concept->_get(&_model); }

        explicit operator bool() const noexcept { return get() != nullptr; }

        bool is_inlined() const { return _concept->_is_inlined(); }

    private:


        template <typename, size_t>
        friend class unique_ptr;
    };


} // namespace poly_v2


