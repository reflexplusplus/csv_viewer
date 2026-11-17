#include "app.h"

#include <algorithm>

REFLEX_BEGIN_INTERNAL(csv_viewer)

constexpr UInt16 kChunkVersion = 2;

enum class ColumnType
{
	Number,
	Date,
	Text
};

struct DecimalKey
{
	WString::View integer;
	WString::View fraction;
	bool negative = false;
};

struct CellSortKey
{
	WString::View text;
	DecimalKey number;
	UInt32 date = 0;
};

bool IsDecimalDigit(WChar ch)
{
	return L'0' <= ch && ch <= L'9';
}

bool ReadDecimal(WString::View text, DecimalKey & key)
{
	bool negative = text[0] == L'-';
	UInt first = negative ? 1 : 0;
	UInt end = first;
	while (end < text.size && IsDecimalDigit(text[end])) ++end;
	if (end == first) return false;

	while (first < end && text[first] == L'0') ++first;
	key.integer = Mid<true>(text, first, end - first);
	key.fraction = {};

	if (end < text.size)
	{
		if (text[end] != L'.' || end + 1 == text.size) return false;
		first = end + 1;
		for (UInt i = first; i < text.size; ++i) if (!IsDecimalDigit(text[i])) return false;
		end = text.size;
		while (end > first && text[end - 1] == L'0') --end;
		key.fraction = Mid<true>(text, first, end - first);
	}

	key.negative = negative && (key.integer || key.fraction);
	return true;
}

bool ReadDate(WString::View text, UInt32 & date)
{
	if (text.size != 10 || text[4] != L'-' || text[7] != L'-') return false;
	for (UInt i = 0; i < text.size; ++i) if (i != 4 && i != 7 && !IsDecimalDigit(text[i])) return false;

	UInt year = 0;
	for (UInt i = 0; i < 4; ++i) year = year * 10 + UInt(text[i] - L'0');
	UInt month = UInt(text[5] - L'0') * 10 + UInt(text[6] - L'0');
	UInt day = UInt(text[8] - L'0') * 10 + UInt(text[9] - L'0');
	if (year == 0 || month == 0 || month > 12 || day == 0) return false;

	constexpr UInt days_per_month[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
	bool leap_day = month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
	if (day > days_per_month[month - 1] + UInt(leap_day)) return false;

	date = year * 10000 + month * 100 + day;
	return true;
}

template <class TYPE> Int CompareValues(const TYPE & left, const TYPE & right)
{
	if (left < right) return -1;
	return right < left ? 1 : 0;
}

Int CompareMagnitude(const DecimalKey & left, const DecimalKey & right)
{
	if (Int order = CompareValues(left.integer.size, right.integer.size)) return order;
	if (Int order = CompareValues(left.integer, right.integer)) return order;
	return CompareValues(left.fraction, right.fraction);
}

Int CompareDecimals(const DecimalKey & left, const DecimalKey & right)
{
	if (left.negative != right.negative) return left.negative ? -1 : 1;
	return left.negative ? CompareMagnitude(right, left) : CompareMagnitude(left, right);
}

ColumnType ReadSortKeys(ArrayRegion<CellSortKey> keys, const App::Table & table, UInt column)
{
	bool numbers = true;
	bool dates = true;
	for (UInt index = 0; index < keys.size; ++index)
	{
		const auto & record = table.records[index + 1];
		auto & key = keys[index];
		key.text = column < record.GetSize() ? ToView(record[column]) : WString::View();
		if (!key.text) continue;
		if (numbers) numbers = ReadDecimal(key.text, key.number);
		if (dates) dates = ReadDate(key.text, key.date);
	}

	if (numbers) return ColumnType::Number;
	return dates ? ColumnType::Date : ColumnType::Text;
}

Int CompareSortKeys(const CellSortKey & left, const CellSortKey & right, ColumnType type)
{
	if (type == ColumnType::Number) return CompareDecimals(left.number, right.number);
	if (type == ColumnType::Date) return CompareValues(left.date, right.date);
	return CompareValues(left.text, right.text);
}

bool IsUtf8Text(Data::Archive::View bytes)
{
	UInt pos = 0;
	while (pos < bytes.size)
	{
		UInt8 lead = bytes[pos++];
		if (lead == 0) return false;
		if (lead < 0x80) continue;
		if (lead < 0xc2 || lead > 0xf4) return false;

		UInt count = lead < 0xe0 ? 1 : (lead < 0xf0 ? 2 : 3);
		if (bytes.size - pos < count) return false;

		UInt8 next = bytes[pos];
		if ((lead == 0xe0 && next < 0xa0) || (lead == 0xed && next >= 0xa0)) return false;
		if ((lead == 0xf0 && next < 0x90) || (lead == 0xf4 && next >= 0x90)) return false;
		for (UInt i = 0; i < count; ++i) if ((bytes[pos++] & 0xc0) != 0x80) return false;
	}

	return true;
}

bool IsRecordEnd(WChar ch)
{
	return ch == L'\r' || ch == L'\n';
}

bool ReadField(WString::View text, UInt & pos, WString & field)
{
	if (pos < text.size && text[pos] == L'"')
	{
		++pos;
		while (pos < text.size)
		{
			WChar ch = text[pos++];
			if (ch == L'"')
			{
				if (pos == text.size || text[pos] != L'"') return true;
				++pos;
			}
			field.Push(ch);
		}
		return false;
	}

	UInt start = pos;
	while (pos < text.size && text[pos] != L',' && !IsRecordEnd(text[pos]))
	{
		if (text[pos] == L'"') return false;
		++pos;
	}
	field = Mid<true>(text, start, pos - start);
	return true;
}

bool ParseCSV(WString::View text, App::Table & table)
{
	UInt pos = 0;
	while (pos < text.size)
	{
		App::Row record;
		for (;;)
		{
			if (!ReadField(text, pos, record.Push(WString()))) return false;
			if (pos == text.size) break;

			WChar delimiter = text[pos++];
			if (delimiter == L',') continue;
			if (!IsRecordEnd(delimiter)) return false;
			if (delimiter == L'\r' && pos < text.size && text[pos] == L'\n') ++pos;
			break;
		}
		table.column_count = Max(table.column_count, record.GetSize());
		table.records.Push(std::move(record));
	}

	return true;
}

bool NeedsCsvQuotes(WString::View field)
{
	for (auto ch : field) if (ch == L',' || ch == L'"' || IsRecordEnd(ch)) return true;
	return false;
}

void WriteCsvRow(Data::Archive & bytes, const App::Row & row, WString & line)
{
	line.Clear();
	for (UInt column = 0; column < row.GetSize(); ++column)
	{
		if (column) line.Push(L',');
		const auto & field = row[column];
		if (NeedsCsvQuotes(field))
		{
			line.Push(L'"');
			for (auto ch : field)
			{
				if (ch == L'"') line.Push(ch);
				line.Push(ch);
			}
			line.Push(L'"');
		}
		else line.Append(field);
	}
	line.Append(L"\r\n");
	Data::EncodeUTF8(bytes, line);
}

void StoreText(Data::Archive & stream, WString::View text)
{
	Data::Marker<UInt32> size(stream);
	Data::EncodeUTF8(stream, text);
	size.Set(size.GetDelta());
}

bool ReadText(Data::Archive::View & stream, WString & text)
{
	if (stream.size < sizeof(UInt32)) return false;
	auto size = Data::Deserialize<UInt32>(stream);
	if (size > stream.size) return false;
	if (size == 0) return true;

	auto bytes = Data::ReadBytes(stream, size);
	if (!IsUtf8Text(bytes)) return false;
	Data::DecodeUTF8(text, bytes);
	return true;
}

bool ReadRecords(Data::Archive::View & stream, App::Table & table)
{
	if (stream.size < sizeof(UInt32)) return false;
	auto rows = Data::Deserialize<UInt32>(stream);
	if (rows > stream.size / sizeof(UInt32)) return false;

	for (UInt32 row = 0; row < rows; ++row)
	{
		if (stream.size < sizeof(UInt32)) return false;
		auto columns = Data::Deserialize<UInt32>(stream);
		if (columns == 0 || columns > stream.size / sizeof(UInt32)) return false;

		auto & record = table.records.Push();
		for (UInt32 column = 0; column < columns; ++column) if (!ReadText(stream, record.Push())) return false;
		table.column_count = Max(table.column_count, columns);
	}

	return true;
}

REFLEX_END_INTERNAL

csv_viewer::App::App()
	: Bootstrap::App(MakeKey32("csv_viewer"), kChunkVersion)
	, m_source_clock(Async::CreatePeriodicClock(1.0f, [this]() { CheckSource(); }))
{
}

Reflex::Unretained<csv_viewer::App> csv_viewer::App::Create()
{
	return New<App>();
}

Reflex::WString csv_viewer::App::OpenCsv(WString path)
{
	auto file = Make<System::FileHandle>(path);
	if (!file) return L"Could not open the CSV file.";

	UInt64 size = file->GetSize();
	if (size > kMaxUInt32) return L"The CSV file is too large.";
	auto bytes = File::ReadBytes(file, UInt(size));
	if (bytes.GetSize() != size) return L"Could not read the entire CSV file.";

	Data::Archive::View utf8 = bytes;
	if (utf8.size >= 3 && utf8[0] == 0xef && utf8[1] == 0xbb && utf8[2] == 0xbf) utf8 = Mid<true>(utf8, 3);
	if (!IsUtf8Text(utf8)) return L"The CSV file must contain valid UTF-8 text.";

	Table table;
	if (utf8.size && !ParseCSV(Data::DecodeUTF8(utf8), table)) return L"The CSV file contains an invalid quoted field.";

	m_table = std::move(table);
	SetSource(std::move(path), bytes);
	return {};
}

bool csv_viewer::App::SaveCsv(WString path)
{
	constexpr UInt8 kUtf8Bom[] = { 0xef, 0xbb, 0xbf };
	Data::Archive bytes(ToView(kUtf8Bom));
	WString line;
	for (const auto & row : m_table.records) WriteCsvRow(bytes, row, line);

	if (!File::Save(path, bytes)) return false;
	SetSource(std::move(path), bytes);
	return true;
}

bool csv_viewer::App::SortColumn(UInt column, bool descending)
{
	if (column >= m_table.column_count) return false;

	auto & records = m_table.records;
	UInt count = records.GetSize() - 1;
	if (count < 2) return true;

	Array<CellSortKey> keys(count);
	auto type = ReadSortKeys(ToRegion(keys), m_table, column);
	Array<UInt> order(count);
	for (UInt index = 0; index < count; ++index) order[index] = index;

	// Reflex::Sort uses the last element as its pivot and can take quadratic time on ordered input.
	std::sort(order.begin(), order.end(), [&keys, type, descending](UInt left, UInt right)
	{
		const auto & a = keys[left];
		const auto & b = keys[right];
		if (bool(a.text) != bool(b.text)) return bool(a.text);
		if (a.text)
		{
			if (Int order = CompareSortKeys(a, b, type)) return descending ? order > 0 : order < 0;
		}
		return left < right;
	});

	Array<Row> sorted;
	sorted.Reserve(records.GetSize());
	sorted.Push(std::move(records[0]));
	for (auto index : order) sorted.Push(std::move(records[index + 1]));
	records = std::move(sorted);
	Notify(false);
	return true;
}

void csv_viewer::App::SetSource(WString path, Data::Archive::View bytes)
{
	m_path = std::move(path);
	m_source_hash.Set(Data::FNV1a64(bytes));
	m_source_changed = false;
	Notify(false);
}

void csv_viewer::App::CheckSource()
{
	if (!m_path) return;
	auto file = Make<System::FileHandle>(m_path);
	if (!file) return;

	UInt64 remaining = file->GetSize();
	if (remaining > kMaxUInt32) return;
	UInt64 hash = Data::FNV1a64({});
	UInt8 buffer[64 * 1024];
	while (remaining)
	{
		UInt count = Min(UInt(remaining), UInt(sizeof(buffer)));
		if (file->Read(buffer, count) != count) return;
		hash = Data::FNV1a64({ buffer, count }, hash);
		remaining -= count;
	}

	bool changed = !m_source_hash || hash != m_source_hash.value;
	// Keep notifying while stale so Auto can retry a failed reload on a later poll.
	if (SetFiltered(m_source_changed, changed) || changed) Notify(false);
}

bool csv_viewer::App::RestoreTable(Data::Archive::View & stream, UInt16 version)
{
	WString path;
	Table table;
	if (!ReadText(stream, path) || !ReadRecords(stream, table)) return false;

	Optional<UInt64> hash;
	if (version >= 2 && stream.size == sizeof(UInt64)) hash.Set(Data::Deserialize<UInt64>(stream));
	if (stream.size) return false;

	m_table = std::move(table);
	m_path = std::move(path);
	m_source_hash = hash;
	m_source_changed = false;
	return true;
}

void csv_viewer::App::OnRestore(Data::Archive::View & stream, Key32 context)
{
	if (!RestoreTable(stream, kChunkVersion)) output.Warn("Could not restore the CSV table from the app session");
}

bool csv_viewer::App::OnImport(UInt16 version, Data::Archive::View & stream, Key32 context)
{
	return version == 1 && RestoreTable(stream, version);
}

void csv_viewer::App::OnStore(Data::Archive & stream) const
{
	StoreText(stream, m_path);
	Data::Serialize(stream, m_table.records.GetSize());
	for (const auto & record : m_table.records)
	{
		Data::Serialize(stream, record.GetSize());
		for (const auto & field : record) StoreText(stream, field);
	}
	if (m_source_hash) Data::Serialize(stream, m_source_hash.value);
}

Reflex::Output csv_viewer::output("csv_viewer");
