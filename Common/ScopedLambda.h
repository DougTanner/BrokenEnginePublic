#pragma once

namespace common
{

class ScopedLambda
{
public:

	ScopedLambda(std::move_only_function<void()> releaseFunction)
	: mReleaseFunction(std::move(releaseFunction))
	{
	}

	~ScopedLambda()
	{
		try
		{
			if (mReleaseFunction)
			{
				mReleaseFunction();
			}
		}
		catch(...)
		{
		}
	}

	ScopedLambda() = delete;

private:

	std::move_only_function<void()> mReleaseFunction;
};

} // namespace common
