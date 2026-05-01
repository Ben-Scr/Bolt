#pragma once

#include "Core/Export.hpp"
#include "Core/Layer.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

namespace Axiom {

	class AXIOM_API LayerStack {
	public:
		using Storage = std::vector<std::unique_ptr<Layer>>;
		using iterator = Storage::iterator;
		using const_iterator = Storage::const_iterator;
		using reverse_iterator = Storage::reverse_iterator;
		using const_reverse_iterator = Storage::const_reverse_iterator;

		void PushLayer(std::unique_ptr<Layer> layer) {
			auto insertIt = m_Layers.begin() + static_cast<std::ptrdiff_t>(m_InsertIndex);
			m_Layers.insert(insertIt, std::move(layer));
			++m_InsertIndex;
		}

		void PushOverlay(std::unique_ptr<Layer> overlay) {
			m_Layers.push_back(std::move(overlay));
		}

		bool PopLayer(Layer* layer) {
			auto begin = m_Layers.begin();
			auto end = begin + static_cast<std::ptrdiff_t>(m_InsertIndex);
			auto it = std::find_if(begin, end, [layer](const std::unique_ptr<Layer>& entry) {
				return entry.get() == layer;
			});
			if (it == end) {
				return false;
			}

			m_Layers.erase(it);
			--m_InsertIndex;
			return true;
		}

		bool PopOverlay(Layer* layer) {
			auto begin = m_Layers.begin() + static_cast<std::ptrdiff_t>(m_InsertIndex);
			auto it = std::find_if(begin, m_Layers.end(), [layer](const std::unique_ptr<Layer>& entry) {
				return entry.get() == layer;
			});
			if (it == m_Layers.end()) {
				return false;
			}

			m_Layers.erase(it);
			return true;
		}

		void Clear() {
			m_Layers.clear();
			m_InsertIndex = 0;
		}

		bool Empty() const { return m_Layers.empty(); }
		std::size_t Size() const { return m_Layers.size(); }

		iterator begin() { return m_Layers.begin(); }
		iterator end() { return m_Layers.end(); }
		const_iterator begin() const { return m_Layers.begin(); }
		const_iterator end() const { return m_Layers.end(); }
		const_iterator cbegin() const { return m_Layers.cbegin(); }
		const_iterator cend() const { return m_Layers.cend(); }

		reverse_iterator rbegin() { return m_Layers.rbegin(); }
		reverse_iterator rend() { return m_Layers.rend(); }
		const_reverse_iterator rbegin() const { return m_Layers.rbegin(); }
		const_reverse_iterator rend() const { return m_Layers.rend(); }
		const_reverse_iterator crbegin() const { return m_Layers.crbegin(); }
		const_reverse_iterator crend() const { return m_Layers.crend(); }

	private:
		Storage m_Layers;
		std::size_t m_InsertIndex = 0;
	};

} // namespace Axiom
