#pragma once

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

// ------- RawMemory -------
template <typename T>
class RawMemory {
public:
	RawMemory() = default;

	RawMemory(const RawMemory&) = delete;

	RawMemory(RawMemory&& other) noexcept {
		*this = std::move(other);
	}

	explicit RawMemory(size_t capacity)
		: buffer_(Allocate(capacity))
		, capacity_(capacity) {
	}

	RawMemory& operator=(const RawMemory& rhs) = delete;

	RawMemory& operator=(RawMemory&& rhs) noexcept {
		this->buffer_ = std::exchange(rhs.buffer_, nullptr);
		this->capacity_ = rhs.capacity_;
		return *this;
	}

	~RawMemory() {
		Deallocate(buffer_);
	}

	T* operator+(size_t offset) noexcept {
		// Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
		assert(offset <= capacity_);
		return buffer_ + offset;
	}

	const T* operator+(size_t offset) const noexcept {
		return const_cast<RawMemory&>(*this) + offset;
	}

	const T& operator[](size_t index) const noexcept {
		return const_cast<RawMemory&>(*this)[index];
	}

	T& operator[](size_t index) noexcept {
		assert(index < capacity_);
		return buffer_[index];
	}

	void Swap(RawMemory& other) noexcept {
		std::swap(buffer_, other.buffer_);
		std::swap(capacity_, other.capacity_);
	}

	const T* GetAddress() const noexcept {
		return buffer_;
	}

	T* GetAddress() noexcept {
		return buffer_;
	}

	size_t Capacity() const {
		return capacity_;
	}

private:
	// Выделяет сырую память под n элементов и возвращает указатель на неё
	static T* Allocate(size_t n) {
		return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
	}

	// Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
	static void Deallocate(T* buf) noexcept {
		operator delete(buf);
	}

	T* buffer_ = nullptr;
	size_t capacity_ = 0;
};

// ------- Vector -------
template <typename T>
class Vector {
public:
	using iterator = T*;
	using const_iterator = const T*;

	iterator begin() noexcept {
		return data_.GetAddress();
	}
	iterator end() noexcept {
		return data_.GetAddress() + size_;
	}
	const_iterator begin() const noexcept{
		return data_.GetAddress();
	}
	const_iterator end() const noexcept{
		return data_.GetAddress() + size_;
	}
	const_iterator cbegin() const noexcept {
		return begin();
	}
	const_iterator cend() const noexcept {
		return end();
	}

public:
	// Конструктор по-умолчанию
	Vector() = default;

	// Конструктор вектора заданного размера со значениями по-умолчанию
	explicit Vector(size_t size)
		: data_(size)
		, size_(size) {
		std::uninitialized_value_construct_n(data_.GetAddress(), size);
	}

	// Конструктор копирования
	Vector(const Vector& other)
		: data_(other.size_)
		, size_(other.size_) {
		std::uninitialized_copy_n(other.data_.GetAddress(), size_, this->data_.GetAddress());
	}

	// Конструктор перемещения
	Vector(Vector&& other) noexcept
		: data_(std::move(other.data_))
		, size_(std::exchange(other.size_, 0)) {}

	Vector& operator=(const Vector& rhs) {
		if (this != &rhs) {
			if (rhs.size_ > data_.Capacity()) {
				Vector rhs_copy(rhs);
				Swap(rhs_copy);
			} else {
				if (rhs.size_ < this->size_) {
					auto it = std::copy_n(rhs.data_.GetAddress(), rhs.size_, this->data_.GetAddress());
					std::destroy_n(it, this->size_ - rhs.size_);
				} else {
					auto it = std::copy_n(rhs.data_.GetAddress(), this->size_, this->data_.GetAddress());
					std::uninitialized_copy_n(rhs.data_.GetAddress() + this->size_, rhs.size_ - this->size_, it);
				}
				this->size_ = rhs.size_;
			}
		}
		return *this;
	}

	Vector& operator=(Vector&& rhs) noexcept {
		this->Swap(rhs);
		return *this;
	}

	~Vector() {
		std::destroy_n(data_.GetAddress(), size_);
	}

	size_t Capacity() const noexcept {
		return data_.Capacity();
	}

	void PopBack() noexcept {
		std::destroy_at(end() - 1);
		--size_;
	}

	template <typename... Args>
	T& EmplaceBack(Args&&... args) {
		if (size_ < data_.Capacity()) {
			new (data_.GetAddress() + size_) T(std::forward<Args>(args)...);
		} else {
			size_t new_capacity = size_ == 0 ? 1 : data_.Capacity() * 2;
			RawMemory<T> new_data(new_capacity);
			new (new_data.GetAddress() + size_) T(std::forward<Args>(args)...);
			ReallocateData(new_data);
		}
		++size_;
		return data_[size_ - 1];
	}

	template <typename... Args>
	iterator Emplace(const_iterator pos, Args&&... args) {
		if (pos == end()) {
			return &EmplaceBack(std::forward<Args>(args)...);
		}
		if (size_ < data_.Capacity()) {
			size_t pos_index = pos - begin();
			T copy(std::forward<Args>(args)...);
			new (end()) T(std::move(*(end() - 1)));
			std::move_backward(begin() + pos_index, end() - 1, end());
			data_[pos_index] = std::move(copy);
			++size_;
			return begin() + pos_index;
		} else {
			size_t new_capacity = size_ == 0 ? 1 : data_.Capacity() * 2;
			RawMemory<T> new_data(new_capacity);
			size_t pos_index = pos - begin();
			new (new_data.GetAddress() + pos_index) T(std::forward<Args>(args)...);
			try {
				ReallocatePartial(begin(), pos_index, new_data.GetAddress());
			} catch (...) {
				new_data[pos_index].~T();
				throw;
			}
			try {
				ReallocatePartial(begin() + pos_index, size_ - pos_index, new_data.GetAddress() + pos_index + 1);
			} catch (...) {
				std::destroy_n(new_data.GetAddress(), pos_index + 1);
				throw;
			}
			std::destroy_n(begin(), size_);
			data_.Swap(new_data);
			++size_;
			return begin() + pos_index;
		}
		assert(false);
		return nullptr;
	}

	iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
		size_t pos_index = pos - begin();
		std::move(begin() + pos_index + 1, end(), begin() + pos_index);
		std::destroy_at(end() - 1);
		--size_;
		return begin() + pos_index;
	}

	iterator Insert(const_iterator pos, const T& value) {
		return Emplace(pos, value);
	}

	iterator Insert(const_iterator pos, T&& value) {
		return Emplace(pos, std::move(value));
	}

	void PushBack(T&& value) {
		EmplaceBack(std::move(value));
	}

	void PushBack(const T& value) {
		EmplaceBack(value);
	}

	void Reserve(size_t new_capacity) {
		if (new_capacity <= data_.Capacity()) {
			return;
		}
		RawMemory<T> new_data(new_capacity);
		ReallocateData(new_data);
	}

	void Resize(size_t new_size) {
		if (new_size == size_) {
			return;
		}
		if (new_size < size_) {
			std::destroy_n(begin() + new_size, size_ - new_size);
		} else {
			Reserve(new_size);
			std::uninitialized_value_construct_n(end(), new_size - size_);
		}
		size_ = new_size;
	}

	size_t Size() const noexcept {
		return size_;
	}

	void Swap(Vector& other) noexcept {
		this->data_.Swap(other.data_);
		std::swap(this->size_, other.size_);
	}

	const T& operator[](size_t index) const noexcept {
		return const_cast<Vector&>(*this)[index];
	}

	T& operator[](size_t index) noexcept {
		assert(index < size_);
		return data_[index];
	}

private:
	RawMemory<T> data_;
	size_t size_ = 0;

	void ReallocateData(RawMemory<T>& new_data) {
		if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
			std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
		} else {
			std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
		}
		std::destroy_n(data_.GetAddress(), size_);
		data_.Swap(new_data);
	}

	static void ReallocatePartial(T* from, size_t count, T* to) {
		if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
			std::uninitialized_move_n(from, count, to);
		} else {
			std::uninitialized_copy_n(from, count, to);
		}
	}
};