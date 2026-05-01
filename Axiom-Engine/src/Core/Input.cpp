#include "pch.hpp"
#include "Input.hpp"

#include <algorithm>

namespace Axiom {
    bool Input::GetKey(KeyCode keycode) const {
        const int i = static_cast<int>(keycode);
        return (i >= 0 && i < k_KeyCount) ? m_CurrentKeyStates[i] : false;
    }

    bool Input::GetKeyDown(KeyCode keycode) const {
        const int i = static_cast<int>(keycode);
        return (i >= 0 && i < k_KeyCount) && m_CurrentKeyStates[i] && !m_PreviousKeyStates[i];
    }

    bool Input::GetKeyUp(KeyCode keycode) const {
        const int i = static_cast<int>(keycode);
        return (i >= 0 && i < k_KeyCount) && !m_CurrentKeyStates[i] && m_PreviousKeyStates[i];
    }

    bool Input::GetMouse(MouseButton keycode) const {
        const int btn = static_cast<int>(keycode);
        return (btn >= 0 && btn < k_MouseCount) ? m_CurrentMouseButtons[btn] : false;
    }

    bool Input::GetMouseDown(MouseButton keycode) const {
        const int btn = static_cast<int>(keycode);
        return (btn >= 0 && btn < k_MouseCount) && m_CurrentMouseButtons[btn] && !m_PreviousMouseButtons[btn];
    }

    bool Input::GetMouseUp(MouseButton keycode) const {
        const int btn = static_cast<int>(keycode);
        return (btn >= 0 && btn < k_MouseCount) && !m_CurrentMouseButtons[btn] && m_PreviousMouseButtons[btn];
    }

    void Input::Update() {
        std::copy(m_CurrentKeyStates.begin(), m_CurrentKeyStates.end(), m_PreviousKeyStates.begin());
        std::copy(m_CurrentMouseButtons.begin(), m_CurrentMouseButtons.end(), m_PreviousMouseButtons.begin());

        m_ScrollValue = 0.f;
        m_MouseDelta = { 0, 0 };

        const bool inputRight = GetKey(KeyCode::D) || GetKey(KeyCode::Right);
        const bool inputUp = GetKey(KeyCode::W) || GetKey(KeyCode::Up);
        const bool inputDown = GetKey(KeyCode::S) || GetKey(KeyCode::Bottom);
        const bool inputLeft = GetKey(KeyCode::A) || GetKey(KeyCode::Left);

        const float x = inputRight ? 1.f : (inputLeft ? -1.f : 0.f);
        const float y = inputUp ? 1.f : (inputDown ? -1.f : 0.f);
        m_Axis = { x, y };
    }

    void Input::OnKeyDown(int key) {
        if (key >= 0 && key < k_KeyCount)
            m_CurrentKeyStates[key] = true;
    }

    void Input::OnKeyUp(int key) {
        if (key >= 0 && key < k_KeyCount)
            m_CurrentKeyStates[key] = false;
    }

    void Input::OnMouseDown(int btn) {
        if (btn >= 0 && btn < k_MouseCount)
            m_CurrentMouseButtons[btn] = true;
    }

    void Input::OnMouseUp(int btn) {
        if (btn >= 0 && btn < k_MouseCount)
            m_CurrentMouseButtons[btn] = false;
    }

    void Input::OnMouseMove(double x, double y) {
        const Vec2 pos{ static_cast<float>(x), static_cast<float>(y) };
        m_MouseDelta = pos - m_MousePosition;
        m_MousePosition = pos;
    }
}