#pragma once

#include "reflex_ext.h"

namespace csv_viewer
{
	using namespace Reflex;

	class App : public Bootstrap::App
	{
	public:
		using Row = Array<WString>;

		struct Table
		{
			Array<Row> records;
			UInt column_count = 0;
		};

		static constexpr WString::View kFileExt = L"csv";

		static Unretained<App> Create();
		App();

		// Returns an empty string on success. A failed load leaves the current table intact.
		WString OpenCsv(WString path);
		bool SaveCsv(WString path);
		bool SortColumn(UInt column, bool descending);
		const Table & GetTable() const { return m_table; }
		const WString & GetCsvPath() const { return m_path; }
		bool SourceChanged() const { return m_source_changed; }

	private:
		void CheckSource();
		void SetSource(WString path, Data::Archive::View bytes);
		bool RestoreTable(Data::Archive::View & stream, UInt16 version);
		void OnRestore(Data::Archive::View & stream, Key32 context) override;
		bool OnImport(UInt16 version, Data::Archive::View & stream, Key32 context) override;
		void OnStore(Data::Archive & stream) const override;

		Table m_table;
		WString m_path;
		Optional<UInt64> m_source_hash;
		bool m_source_changed = false;
		Reference<Object> m_source_clock;
	};

	extern Output output;
}
