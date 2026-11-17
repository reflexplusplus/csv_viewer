#pragma once

#include "app.h"

namespace csv_viewer
{
	class View : public Bootstrap::View
	{
	public:
		static Unretained<View> Create(App & app);

	protected:
		using Bootstrap::View::View;
	};
}
