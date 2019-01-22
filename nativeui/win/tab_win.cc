// Copyright 2019 Cheng Zhao. All rights reserved.
// Use of this source code is governed by the license that can be found in the
// LICENSE file.

#include "nativeui/tab.h"

#include <numeric>

#include "base/strings/utf_string_conversions.h"
#include "nativeui/events/event.h"
#include "nativeui/gfx/geometry/size_conversions.h"
#include "nativeui/gfx/win/text_win.h"
#include "nativeui/win/container_win.h"

namespace nu {

namespace {

// Draws the tab items.
class TabItem : public ViewImpl {
 public:
  static const int kHPadding = 2;
  static const int kVPadding = 1;
  static const int kTPadding = 1;

  TabItem() : ViewImpl(ControlType::View, nullptr) {}

  ~TabItem() {}

  void SetTitle(base::string16 title) {
    title_ = std::move(title);
    size_ = ToCeiledSize(MeasureText(title_, font()));
    size_.Enlarge(2 * kHPadding * scale_factor(),
                  2 * kVPadding * scale_factor());
    Invalidate();
  }

  void SetSelected(bool selected) {
    selected_ = selected;
    SetState(selected_ ? ControlState::Pressed : ControlState::Normal);
  }

  bool selected() const { return selected_; }
  Size size() const { return size_; }

  std::function<void(TabItem*)> on_select;

 protected:
  // ViewImpl:
  void Draw(PainterWin* painter, const Rect& dirty) override {
    // The selected item overflows 1-pixel on bottom, so the border of tab
    // panel won't show.
    Rect rect(size_allocation().size());
    if (!selected_)
      rect.Inset(0, 0, 0, 1);

    // Draw background.
    NativeTheme::ExtraParams params;
    painter->DrawNativeTheme(NativeTheme::Part::TabItem, state(), rect, params);

    // Draw title.
    TextAttributes attr(font(), color(), TextAlign::Center, TextAlign::Center);
    painter->DrawTextPixel(title_, rect, attr);
  }

  void OnMouseEnter(NativeEvent event) override {
    if (!selected_) {
      SetState(ControlState::Hovered);
      Invalidate();
    }
    ViewImpl::OnMouseEnter(event);
  }

  void OnMouseLeave(NativeEvent event) override {
    if (!selected_) {
      SetState(ControlState::Normal);
      Invalidate();
    }
    ViewImpl::OnMouseLeave(event);
  }

  bool OnMouseClick(NativeEvent event) override {
    if (ViewImpl::OnMouseClick(event))
      return true;
    if (MouseEvent(event, this).type == EventType::MouseDown)
      on_select(this);
    return true;
  }

 private:
  bool selected_ = false;
  base::string16 title_;
  Size size_;
};

// Implementation of the whole tab area, which draws the tab panel.
class TabImpl : public ContainerImpl,
                public ContainerImpl::Adapter {
 public:
  static const int kContentPadding = 5;

  explicit TabImpl(Tab* delegate) : ContainerImpl(delegate, this) {}

  void AddPage(base::string16 title) {
    std::unique_ptr<TabItem> item(new TabItem);
    item->SetTitle(std::move(title));
    item->on_select = [this](TabItem* item) { this->SetSelectedItem(item); };
    items_.emplace_back(std::move(item));
    if (items_.size() == 1)
      SetSelectedItem(items_[0].get());
    Layout();
  }

  void RemovePageAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= items_.size())
      return;

    // Select next item automatically.
    if (index == selected_item_index_) {
      if (items_.size() == 1) {
        selected_item_index_ = -1;
        selected_item_ = nullptr;
      } else {
        selected_item_index_ = (index + 1) % items_.size();
        selected_item_ = items_[selected_item_index_].get();
        selected_item_->SetSelected(true);
      }
    }

    // Destroy the item.
    items_.erase(items_.begin() + index);
    Layout();
  }

  void SelectItemAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= items_.size())
      return;
    SetSelectedItem(items_[index].get());
  }

  void SetSelectedItem(TabItem* item) {
    auto* tab = static_cast<Tab*>(delegate());
    if (selected_item_) {
      selected_item_->SetSelected(false);
      tab->PageAt(selected_item_index_)->SetVisible(false);
    }

    selected_item_ = item;
    selected_item_index_ = std::distance(
        items_.begin(),
        std::find_if(items_.begin(), items_.end(),
                     [item](auto& it) { return it.get() == item; }));
    selected_item_->SetSelected(true);
    tab->PageAt(selected_item_index_)->SetVisible(true);
    Layout();
    tab->on_selected_page_change.Emit(tab);
  }

  ViewImpl* GetSelectedPage() const {
    View* content = static_cast<Tab*>(delegate())->PageAt(selected_item_index_);
    return content ? content->GetNative() : nullptr;
  }

  Size GetMinimumSize() const {
    int width = std::accumulate(
        items_.begin(), items_.end(), 0,
        [](int a, auto& b) { return a + b->size_allocation().width(); });
    Size size(width, GetItemsHeight());
    size.Enlarge(2 * kContentPadding * scale_factor(),
                 2 * kContentPadding * scale_factor());
    return size;
  }

  int selected_item_index() const { return selected_item_index_; }

 protected:
  // ContainerImpl:
  void Layout() override {
    // Place items.
    int x = 0;
    for (auto& item : items_) {
      Rect rect(size_allocation().origin(), item->size());
      rect.Offset(x, 0);
      rect.set_height(GetItemsHeight());
      if (!item->selected())
        rect.Inset(0, TabItem::kTPadding * scale_factor(), 0, 0);
      item->set_size_allocation(rect);
      x += rect.width();
    }

    // Place content view.
    ViewImpl* content = GetSelectedPage();
    if (content) {
      Rect rect(size_allocation());
      rect.Inset(kContentPadding * scale_factor(),
                 kContentPadding * scale_factor());
      rect.Inset(0, GetItemsHeight() - 1 * scale_factor(), 0, 0);
      content->SizeAllocate(rect);
    }

    Invalidate();
  }

  void ForEach(const std::function<bool(ViewImpl*)>& callback,
               bool reverse) override {
    if (items_.empty())
      return;
    auto* tab = static_cast<Tab*>(delegate());
    for (int i = reverse ? static_cast<int>(items_.size()) - 1 : 0;
         reverse ? (i >= 0) : (i < items_.size());
         reverse ? --i : ++i) {
      if (!callback(items_[i].get()) ||
          !callback(tab->PageAt(i)->GetNative()))
        break;
    }
  }

  bool HasChild(ViewImpl* child) override {
    if (child == GetSelectedPage())
      return true;
    return std::find_if(items_.begin(), items_.end(), [child](const auto& it) {
      return it.get() == child;
    }) != items_.end();
  }

  // ViewImpl:
  void SetFont(Font* font) override {
    ViewImpl::SetFont(font);
    for (auto& item : items_)
      item->SetFont(font);
    items_height_ = -1;
    Layout();
  }

  void SetColor(Color color) override {
    ViewImpl::SetColor(color);
    for (auto& item : items_)
      item->SetColor(color);
    Invalidate();
  }

  void Draw(PainterWin* painter, const Rect& dirty) override {
    // Draw panel background.
    Rect rect(size_allocation().size());
    if (!items_.empty())
      rect.Inset(0, GetItemsHeight() - 1, 0, 0);
    NativeTheme::ExtraParams params;
    painter->DrawNativeTheme(NativeTheme::Part::TabPanel,
                             state(), rect, params);

    // Draw items.
    for (auto& item : items_)
      DrawChild(item.get(), painter, dirty);

    // Draw content view.
    ViewImpl* content = GetSelectedPage();
    if (content)
      DrawChild(content, painter, dirty);
  }

  void OnDPIChanged() override {
    ContainerImpl::OnDPIChanged();
    items_height_ = -1;
    Layout();
  }

 private:
  int GetItemsHeight() const {
    if (items_height_ == -1) {
      items_height_ =
          std::ceil(MeasureText(L"bp", font()).height()) +
          (2 * TabItem::kVPadding + TabItem::kTPadding) * scale_factor() +
          1;  // leave a 1-pixel line so selected item can overflow
    }
    return items_height_;
  }

  scoped_refptr<Font> item_font_;
  mutable int items_height_ = -1;

  TabItem* selected_item_ = nullptr;
  int selected_item_index_ = -1;
  std::vector<std::unique_ptr<TabItem>> items_;
};

}  // namespace

NativeView Tab::PlatformCreate() {
  return new TabImpl(this);
}

void Tab::PlatformAddPage(const std::string& title, View* view) {
  auto* tab = static_cast<TabImpl*>(GetNative());
  view->GetNative()->SetParent(tab);
  if (PageCount() > 0)  // later added pages are hidden by default
    view->SetVisible(false);
  tab->AddPage(base::UTF8ToUTF16(title));
}

void Tab::PlatformRemovePage(int index, View* view) {
  auto* tab = static_cast<TabImpl*>(GetNative());
  view->GetNative()->SetParent(nullptr);
  tab->RemovePageAt(index);
}

void Tab::SelectPageAt(int index) {
  auto* tab = static_cast<TabImpl*>(GetNative());
  tab->SelectItemAt(index);
}

int Tab::GetSelectedPageIndex() const {
  auto* tab = static_cast<TabImpl*>(GetNative());
  return tab->selected_item_index();
}

SizeF Tab::GetMinimumSize() const {
  TabImpl* tab = static_cast<TabImpl*>(GetNative());
  return ScaleSize(SizeF(tab->GetMinimumSize()), 1.0f / tab->scale_factor());
}

}  // namespace nu
