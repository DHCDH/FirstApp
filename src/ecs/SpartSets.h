#pragma once

#include <core/type_traits.hpp>
#include <vector>
#include <memory>
#include <array>
#include <cassert>
#include <limits>

template <typename T, size_t PageSize, typename = std::enable_if<std::is_integral_v<T>>>
class SparseSets final {
public:
	void Add(T t) {
		m_density.push_back(t);
		assure(t);
		index(t) = m_density.size() - 1;
	}

	void Remove(T t) {
		if (!Contain(t)) return;

		auto& idx = index(t);
		if (idx == m_density.size() - 1) {
			idx = null;
			m_density.pop_back();
		}
		else {
			auto last = m_density.back();
			index(last) = idx;
			std::swap(m_density[idx], m_density.back());
			idx = null;
			m_density.pop_back();
		}
	}

	bool Contain(T t) const {
		assert(t != null);
		auto p = page(t);
		auto o = offset(t);
		return p < m_sparse.size() && m_sparse[p]->at(o) != null;
	}

	void Clear() {
		m_density.clear();
		m_sparse.clear();
	}

	auto begin() { return m_density.begin(); }
	auto end() {return m_density.end();}
	auto begin() const { return m_density.begin(); }
	auto end() const { return m_density.end(); }

private:
	std::vector<T> m_density;
	std::vector<std::unique_ptr<std::array<T, PageSize>>> m_sparse;
	static constexpr T null = std::numeric_limits<T>::max();

	size_t page(T t) const { return t / PageSize; }
	T index(T t) const { return m_sparse[page(t)]->at(offset(t)); }
	T& index(T t) { return m_sparse[page(t)]->at(offset(t)); }
	size_t offset(T t) const { return t % PageSize; }
	void assure(T t) {
		auto p = page(t);
		if (p >= m_sparse.size()) {
			for (size_t i = m_sparse.size(); i <= p; i++) {
				m_sparse.emplace_back(std::make_unique<std::array<T, PageSize>>());
				m_sparse[i]->fill(null);
			}
		}
	}
};
