using LanMessenger.Core.ViewModels;
using Microsoft.UI.Dispatching;

namespace LanMessenger.Services;

public sealed class WinDispatcher : IDispatcher
{
    private readonly DispatcherQueue _dq;
    public WinDispatcher(DispatcherQueue dq) { _dq = dq; }

    public void Post(Action action)
    {
        if (_dq.HasThreadAccess) action();
        else _dq.TryEnqueue(() => action());
    }
}
