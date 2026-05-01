using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Axiom.Collections;
public struct Range
{
    private float min;

    public float Min {
        set
        {
            if (value > Max) throw new ArgumentOutOfRangeException($"Range({value},{Max}) is wrong, min can't be more than max.");
            min = value;
        }
        get
        {
            return min;
        }
    }

    private float max;

    public float Max
    {
        set
        {
            if (value < Min) throw new ArgumentOutOfRangeException($"Range({Min},{value}) is wrong, max can't be less than min.");
            max = value;
        }
        get
        {
            return max;
        }
    }

    public Range(float min, float max)
    {
        if(min > max) throw new ArgumentOutOfRangeException($"Range({min},{max}) is wrong, min can't be more than max.");

        this.Min = min;
        this.Max = max;
    }

    public float Clamp(float value)
    {
        if (value < Min) return Min;
        if (value > Max) return Max;
        return value;
    }
}
