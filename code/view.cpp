#include "view.h"

REFLEX_BEGIN_INTERNAL(csv_viewer)

constexpr UInt16 kChunkVersion = 2;
constexpr Float kDefaultColumnWidth = 192.0f;
constexpr Key32 kCsvCell = "csv_cell";

AlreadyRetained<GLX::Object> AddCell(GLX::Object & row, const App::Row & record, UInt column, const GLX::Style & style)
{
	auto cell = GLX::AddInline(row, GLX::Init(New<GLX::Object>(), style));
	cell->id = kCsvCell;
	if (column < record.GetSize()) GLX::SetText(cell, record[column]);
	return cell;
}

void SetCellWidth(GLX::Object & cell, Float width)
{
	GLX::SetBounds(cell, "column", { width, 0.0f }, { width, GLX::kLarge.h });
}

void PopulateRow(GLX::Object & container, const App::Row & record, ArrayView<Float> widths, const GLX::Style & cell_style)
{
	GLX::SetFlow(container, GLX::kFlowX);
	for (UInt column = 0; column < widths.size; ++column)
	{
		SetCellWidth(AddCell(container, record, column, cell_style), widths[column]);
	}
}

class ViewImpl : public View
{
public:
	explicit ViewImpl(App & app);

private:
	void OnResetState(Key32 context) override;
	void OnRestoreState(Data::Archive::View & stream, Key32 context) override;
	bool OnImport(UInt16 version, Data::Archive::View & stream, Key32 context) override;
	void OnStoreState(Data::Archive & stream) const override;

	bool OnEvent(GLX::Object & source, GLX::Event & event) override;
	void OnSetStyle(const GLX::Style & style) override;
	void OnUpdate() override;
	void SortColumn(UInt column, bool descending);
	void RefreshTable(GLX::Point scroll_position);
	void UpdateColumnWidth(UInt column);
	void UpdateTableWidth(GLX::Point scroll_position);
	void SetScrollPosition(GLX::Point position);
	void ChooseCSV();
	void SaveCSV();
	bool OpenCSV(const WString & path, GLX::Point scroll_position);

	App & m_app;
	Array<Float> m_column_widths;
	bool m_resizing_column = false;
	GLX::Object m_toolbar;
	GLX::Button m_open;
	GLX::Button m_save;
	GLX::Object m_status;
	GLX::Button m_reload;
	GLX::Button m_auto;
	GLX::Split m_header_row;
	GLX::ScrollArea m_header;
	GLX::VirtualList m_rows;
	GLX::ScrollArea m_body;
};

ViewImpl::ViewImpl(App & app)
	: View(app, kChunkVersion, L":res:csv_viewer/styles.glx", true)
	, m_app(app)
{
	GLX::SetText(m_open, L"Open");
	GLX::SetText(m_save, L"Save");
	GLX::SetText(m_status, L"Open a UTF-8 CSV file");
	GLX::SetText(m_reload, L"Reload");
	GLX::SetText(m_auto, L"Auto");
	GLX::BindClick(m_open, [this]() { ChooseCSV(); });
	GLX::BindClick(m_save, [this]() { SaveCSV(); });
	GLX::Activate(m_save, bool(m_app.GetCsvPath()));
	GLX::Activate(m_reload, m_app.SourceChanged());
	GLX::BindClick(m_reload, [this]()
	{
		if (m_app.SourceChanged()) OpenCSV(m_app.GetCsvPath(), m_body.GetView().origin);
	});
	GLX::BindClick(m_auto, [this]()
	{
		GLX::ToggleState(m_auto, GLX::kSelectedState);
		Update();
	});
	GLX::AddInline(m_toolbar, m_open);
	GLX::AddInline(m_toolbar, m_save);
	GLX::AddInlineFlex(m_toolbar, m_status);
	GLX::AddInline(m_toolbar, m_reload);
	GLX::AddInline(m_toolbar, m_auto);
	GLX::AddInline(*this, m_toolbar);

	GLX::SetFlow(m_header_row, GLX::kFlowX);
	m_header_row.behaviour->EnableBounds(false);
	m_header.EnableViewBars(false, false);
	m_header.SetContent(m_header_row);
	m_rows.SetPopulateCallback([this](UInt index, ArrayRegion<GLX::VirtualList::Reference> items, const GLX::Style & style)
	{
		const auto & records = m_app.GetTable().records;
		auto cell_style = style["Cell"];
		for (auto & item : items)
		{
			auto row = GLX::Init(New<GLX::Object>(), style);
			PopulateRow(row, records[index + 1], m_column_widths, cell_style);
			item = row;
			++index;
		}
	});
	m_body.SetContent(m_rows);
	GLX::SyncViewports(m_body, m_header, true, false);
	GLX::AddInline(*this, m_header);
	GLX::AddInlineFlex(*this, m_body);
}

void ViewImpl::OnResetState(Key32 context)
{
	SetScrollPosition(GLX::kOrigin);
}

void ViewImpl::OnRestoreState(Data::Archive::View & stream, Key32 context)
{
	if (stream.size < 2 * sizeof(Float32) + sizeof(UInt32)) return;
	GLX::Point position;
	Data::Deserialize(stream, position.x, position.y);
	auto count = Data::Deserialize<UInt32>(stream);
	if (count != m_column_widths.GetSize() || UInt64(count) * sizeof(Float32) != stream.size)
	{
		SetScrollPosition(position);
		return;
	}

	for (auto & width : m_column_widths)
	{
		auto stored = Data::Deserialize<Float32>(stream);
		if (stored > 0.0f && stored <= GLX::kLarge.w) width = stored;
	}
	RefreshTable(position);
}

bool ViewImpl::OnImport(UInt16 version, Data::Archive::View & stream, Key32 context)
{
	if (version != 1 || stream.size != 2 * sizeof(Float32)) return false;
	GLX::Point position;
	Data::Deserialize(stream, position.x, position.y);
	GLX::Core::Context scope;
	SetScrollPosition(position);
	return true;
}

void ViewImpl::OnStoreState(Data::Archive & stream) const
{
	const auto & position = m_body.GetView().origin;
	Data::Serialize(stream, position.x, position.y);
	Data::Serialize(stream, m_column_widths.GetSize());
	for (auto width : m_column_widths) Data::Serialize(stream, width);
}

bool ViewImpl::OnEvent(GLX::Object & source, GLX::Event & event)
{
	if (source.id == kCsvCell && GLX::IsRightClick(event))
	{
		auto menu = GLX::OpenContextMenu(*this);
		GLX::BindClick(menu->AddItem(L"Copy"), [text = WString(GLX::GetText(source))]() { System::SetClipboard(text); });
		if (source.GetParent() == m_header_row)
		{
			UInt column = GLX::LookupIndex(source).value;
			GLX::BindClick(menu->AddItem(L"Sort ascending"), [this, column]() { SortColumn(column, false); });
			GLX::BindClick(menu->AddItem(L"Sort descending"), [this, column]() { SortColumn(column, true); });
		}
		return true;
	}

	if (source == m_header_row && event.id == GLX::kTransaction)
	{
		auto stage = GLX::GetTransactionStage(event);
		if (stage == GLX::kTransactionStageBegin) m_resizing_column = true;
		else if (stage == GLX::kTransactionStagePerform) UpdateColumnWidth(GLX::GetIndex(event));
		else if (stage == GLX::kTransactionStageEnd || stage == GLX::kTransactionStageCancel) m_resizing_column = false;
		return true;
	}

	return Bootstrap::View::OnEvent(source, event);
}

void ViewImpl::OnSetStyle(const GLX::Style & style)
{
	auto toolbar = style["Toolbar"];
	m_toolbar.SetStyle(toolbar);
	m_open.SetStyle(toolbar["Open"]);
	m_save.SetStyle(toolbar["Save"]);
	m_status.SetStyle(toolbar["Status"]);
	m_reload.SetStyle(toolbar["Reload"]);
	m_auto.SetStyle(toolbar["Auto"]);
	m_header.SetStyle(style["Header"]);
	m_body.SetStyle(style["Body"]);
	RefreshTable(m_body.GetView().origin);
}

void ViewImpl::OnUpdate()
{
	if (GLX::IsSelected(m_auto) && !m_resizing_column && m_app.SourceChanged())
	{
		UInt previous_count = m_rows.GetNumItem();
		if (OpenCSV(m_app.GetCsvPath(), m_body.GetView().origin))
		{
			UInt count = m_rows.GetNumItem();
			if (count && count != previous_count) m_rows.Reveal(count - 1);
		}
	}

	GLX::Activate(m_save, bool(m_app.GetCsvPath()));
	GLX::Activate(m_reload, m_app.SourceChanged());
}

void ViewImpl::SortColumn(UInt column, bool descending)
{
	if (!m_app.SortColumn(column, descending)) return;

	auto count = m_rows.GetNumItem();
	m_rows.ClearItems();
	m_rows.SetNumItem(count, true);
}

void ViewImpl::RefreshTable(GLX::Point scroll_position)
{
	const auto & table = m_app.GetTable();
	m_rows.ClearItems();
	UInt previous_count = m_column_widths.GetSize();
	m_column_widths.SetSize(table.column_count);
	for (UInt column = previous_count; column < table.column_count; ++column) m_column_widths[column] = kDefaultColumnWidth;

	m_header_row.Clear();
	if (!table.records.Empty())
	{
		auto style = m_header_row.GetStyle()["Cell"];
		for (UInt column = 0; column < table.column_count; ++column)
		{
			auto cell = AddCell(m_header_row, table.records[0], column, style);
			m_header_row.behaviour->EnableSplit(cell);
			m_header_row.SetSplitSize(cell, m_column_widths[column]);
		}
	}
	m_rows.SetNumItem(table.records.Empty() ? 0 : table.records.GetSize() - 1, true);
	UpdateTableWidth(scroll_position);

	const auto & path = m_app.GetCsvPath();
	if (path) GLX::SetText(m_status, WString(File::SplitFilename(path).b));
}

void ViewImpl::UpdateColumnWidth(UInt column)
{
	auto cell = GLX::LookupChildAtIndex(m_header_row, column);
	Float width = m_header_row.GetSplitSize(cell);
	if (!SetFiltered(m_column_widths[column], width)) return;

	for (auto & row : m_rows) SetCellWidth(GLX::LookupChildAtIndex(row, column), width);
	UpdateTableWidth(m_body.GetView().origin);
}

void ViewImpl::UpdateTableWidth(GLX::Point scroll_position)
{
	Float width = 0.0f;
	for (auto column_width : m_column_widths) width += column_width;
	GLX::SetBounds(m_rows, "columns", { width, 0.0f });
	SetScrollPosition(scroll_position);
}

void ViewImpl::SetScrollPosition(GLX::Point position)
{
	m_body.SetView({ position, m_body.GetView().size });
}

void ViewImpl::ChooseCSV()
{
	if (auto path = GLX::ShowFileDialog(false, { App::kFileExt }, m_app.GetCsvPath(), L"Open CSV")) OpenCSV(path, GLX::kOrigin);
}

void ViewImpl::SaveCSV()
{
	if (auto path = GLX::ShowFileDialog(true, { App::kFileExt }, m_app.GetCsvPath(), L"Save CSV"))
	{
		if (m_app.SaveCsv(std::move(path))) RefreshTable(m_body.GetView().origin);
		else GLX::SetText(m_status, L"Could not save the CSV file.");
	}
}

bool ViewImpl::OpenCSV(const WString & path, GLX::Point scroll_position)
{
	bool same_source = path == m_app.GetCsvPath();
	if (auto error = m_app.OpenCsv(path))
	{
		GLX::SetText(m_status, std::move(error));
		return false;
	}

	if (!same_source) m_column_widths.Clear();
	RefreshTable(scroll_position);
	return true;
}

REFLEX_END_INTERNAL

Reflex::Unretained<csv_viewer::View> csv_viewer::View::Create(App & app)
{
	return Reflex::New<ViewImpl>(app);
}
